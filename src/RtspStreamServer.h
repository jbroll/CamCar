#ifndef RTSP_STREAM_SERVER_H
#define RTSP_STREAM_SERVER_H

#include "StreamServer.h"
#include "PrefEdit.h"         // device password for RTSP Basic auth
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <list>
#include "CStreamer.h"        // vendored Micro-RTSP (libraries/Micro-RTSP)
#include "CRtspSession.h"

// RTSP/RTP-JPEG streaming (rtsp://<host>:554/mjpeg/1), the NVR-native path, via
// a vendored subset of Micro-RTSP. Like MjpegStreamServer it extends
// StreamServer: own port, own FreeRTOS task, consumes the single producer's
// frames via copyLatestFrame() -- never grabs the camera.

// A CStreamer that pulls finished JPEGs from the producer's shared buffer
// instead of grabbing the sensor (stock OV2640Streamer grabs directly).
class SharedFrameStreamer : public CStreamer {
public:
    explicit SharedFrameStreamer(CameraHandler& cam)
        : CStreamer(cam.frameWidth(), cam.frameHeight()), mCam(cam), mLastSeq(0) {
        mBuf = (uint8_t*)heap_caps_malloc(CAP, MALLOC_CAP_SPIRAM);
    }
    ~SharedFrameStreamer() { if (mBuf) heap_caps_free(mBuf); }

    void streamImage(uint32_t curMsec) override {
        if (!mBuf) return;
        size_t len = 0;
        uint32_t ns = mCam.copyLatestFrame(mBuf, CAP, len, mLastSeq);
        if (ns == mLastSeq || len == 0) return;   // no new frame this tick
        mLastSeq = ns;
        // Keep the RTP/JPEG header in sync with the actual (runtime-variable)
        // resolution, else clients reconstruct the wrong size.
        setDimensions(mCam.frameWidth(), mCam.frameHeight());
        streamFrame(mBuf, len, curMsec);
    }

private:
    static constexpr size_t CAP = 180 * 1024;     // matches SHARED_FRAME_CAP
    CameraHandler& mCam;
    uint8_t* mBuf;
    uint32_t mLastSeq;
};

class RtspStreamServer : public StreamServer {
public:
    RtspStreamServer(CameraHandler& cam, uint16_t port = 554)
        : StreamServer(cam, port, "rtsp_stream", 8192, 1), mStreamer(nullptr) {}

protected:
    void run() override {
        mStreamer = new SharedFrameStreamer(mCam);
        mStreamer->setURI(WiFi.localIP().toString() + ":" + String(554));  // mjpeg/1
        mStreamer->setAuthPassword(PrefEdit::get("device_pass", "camcar"));
        uint32_t lastImg = millis();

        for (;;) {
            mStreamer->handleRequests(0);          // service RTSP control (all sessions)

            uint32_t now = millis();
            // Push at most every ~20ms; streamImage() only actually sends when a
            // new producer frame exists, so the producer's pacing still governs.
            if (mStreamer->anySessions() && (uint32_t)(now - lastImg) >= 20) {
                mStreamer->streamImage(now);
                lastImg = now;
            }

            WiFiClient inc = accept();             // non-blocking
            if (inc) {
                // Pick up a runtime device-password change for new sessions.
                mStreamer->setAuthPassword(PrefEdit::get("device_pass", "camcar"));
                WiFiClient* wc = new WiFiClient(inc);   // persist for the session
                CRtspSession* s = mStreamer->addSession(wc);
                mClients.push_back({s, wc});
                mCam.addStreamClient();            // keep the producer running
            }

            // Reap finished sessions (stock Micro-RTSP leaks these; we own them).
            for (auto it = mClients.begin(); it != mClients.end(); ) {
                if (it->sess->m_stopped) {
                    delete it->sess;               // unlinks from streamer, stops socket
                    delete it->wc;
                    mCam.removeStreamClient();
                    it = mClients.erase(it);
                } else {
                    ++it;
                }
            }

            delay(2);
        }
    }

private:
    struct Client { CRtspSession* sess; WiFiClient* wc; };
    SharedFrameStreamer* mStreamer;
    std::list<Client>    mClients;
};

#endif // RTSP_STREAM_SERVER_H

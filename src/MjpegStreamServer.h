#ifndef MJPEG_STREAM_SERVER_H
#define MJPEG_STREAM_SERVER_H

#include "StreamServer.h"
#include <esp_heap_caps.h>

// HTTP-MJPEG (multipart/x-mixed-replace) streaming on its own port + task, for
// VLC/ffmpeg/NVRs. Replaces the old AsyncWebServer /stream + mjpegFill chunked
// path (which ran on async_tcp and had to block-spin there). Here the pump is a
// plain blocking loop on a dedicated task -- linear code, and a slow client can
// only stall this task, never the WS control channel or the producer.
//
// One client at a time: while a viewer streams, serve() owns the task, so a
// second connection waits in the listen backlog until the first drops. Fine for
// a single-camera car; extend to a client list later if needed.
class MjpegStreamServer : public StreamServer {
public:
    MjpegStreamServer(CameraHandler& cam, uint16_t port = 81)
        : StreamServer(cam, port, "mjpeg_stream", 4096, 1) {}

protected:
    // Matches CameraHandler::SHARED_FRAME_CAP -- copyLatestFrame() drops frames
    // larger than this, so equal sizing means producer frames always fit.
    static constexpr size_t FRAME_CAP = 180 * 1024;

    void run() override {
        // Relay buffer in PSRAM (a JPEG can be ~150KB; keep it off the task
        // stack and out of scarce internal DRAM). Allocated once for the task.
        uint8_t* buf = (uint8_t*)heap_caps_malloc(FRAME_CAP, MALLOC_CAP_SPIRAM);
        for (;;) {
            NetworkClient client = accept();          // non-blocking
            if (!client)  { delay(10); continue; }
            if (!buf)     { client.stop(); continue; } // OOM: refuse cleanly
            serve(client, buf);
        }
    }

    void serve(NetworkClient& client, uint8_t* buf) {
        client.setNoDelay(true);
        mCam.addStreamClient();                       // keep the producer running
        client.print(F("HTTP/1.1 200 OK\r\n"
                       "Access-Control-Allow-Origin: *\r\n"
                       "Cache-Control: no-store\r\n"
                       "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"));

        uint32_t seq = 0;
        size_t   len = 0;
        char     hdr[96];
        while (client.connected()) {
            // Wait for a frame newer than the last we sent. The producer paces
            // publishing, so this poll sleeps ~one frame interval at most.
            uint32_t ns = mCam.copyLatestFrame(buf, FRAME_CAP, len, seq);
            while (ns == seq && client.connected()) {
                delay(5);
                ns = mCam.copyLatestFrame(buf, FRAME_CAP, len, seq);
            }
            if (!client.connected()) break;
            seq = ns;

            int hl = snprintf(hdr, sizeof(hdr),
                "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                (unsigned)len);
            if (client.write((const uint8_t*)hdr, hl) != (size_t)hl) break;
            if (client.write(buf, len) != len) break;   // blocking; fails on close
        }
        client.stop();
        mCam.removeStreamClient();
    }
};

#endif // MJPEG_STREAM_SERVER_H

#ifndef MJPEG_STREAM_SERVER_H
#define MJPEG_STREAM_SERVER_H

#include "StreamServer.h"
#include "PrefEdit.h"          // checkBasicAuth() against the device password
#include <esp_heap_caps.h>
#include <list>
#include <lwip/sockets.h>      // send() + MSG_DONTWAIT (availableForWrite() is unreliable here)
#include <errno.h>

// HTTP-MJPEG (multipart/x-mixed-replace) streaming on its own port + task, for
// VLC/ffmpeg/NVRs. Replaces the old AsyncWebServer /stream + mjpegFill chunked
// path (which ran on async_tcp and had to block-spin there). Here the pump is a
// plain loop on a dedicated task -- it can never stall the WS control channel
// or the producer.
//
// Multi-client, non-blocking multiplex: run() keeps a list of viewers and a
// single staging copy of the latest producer frame (copyLatestFrame() fans the
// JPEG out once, like the WS and RTSP paths). Each client owns a PSRAM buffer
// holding the frame it is currently sending plus a write offset; every loop we
// push as much as the socket will take via ::send(..., MSG_DONTWAIT), which
// returns a short count (the rest resumes next loop) or EAGAIN when full, and
// so never blocks. (We deliberately do NOT gate on availableForWrite() -- it
// returns 0 on this lwip client, which silently stalls every write.) A client
// that drains slowly simply advances its offset slower and, when it finishes a
// frame, jumps straight to the newest staged frame (dropping the ones it
// missed). Because no write blocks, a slow or stalled viewer cannot hold up the
// others (no head-of-line blocking) -- the natural per-client backpressure for
// live MJPEG, where a dropped frame is invisible.
class MjpegStreamServer : public StreamServer {
public:
    MjpegStreamServer(CameraHandler& cam, uint16_t port = 81)
        : StreamServer(cam, port, "mjpeg_stream", 4096, 1) {}

protected:
    // Matches CameraHandler::SHARED_FRAME_CAP -- copyLatestFrame() drops frames
    // larger than this, so equal sizing means producer frames always fit.
    static constexpr size_t FRAME_CAP   = 180 * 1024;
    static constexpr size_t HDR_CAP     = 96;     // per-frame multipart boundary
    static constexpr int    MAX_CLIENTS = 8;      // bounds PSRAM use (~180KB each)

    struct Client {
        NetworkClient sock;
        uint8_t* buf;     // own copy of the frame being sent: [hdr][jpeg], PSRAM
        size_t   len;     // total bytes to send for the current frame
        size_t   off;     // bytes already written
        uint32_t seq;     // producer seq of the frame in buf (0 = none loaded yet)
    };

    // Read the HTTP request head (up to the blank line) into `head`, bounded by
    // a deadline and a byte cap so a silent/garbage client can't park the accept
    // loop. Returns true if the full header block (CRLFCRLF) arrived.
    static bool readRequestHead(NetworkClient& c, String& head) {
        head = "";
        head.reserve(512);
        uint32_t deadline = millis() + 2000;
        while (millis() < deadline && head.length() < 2048) {
            if (c.available()) {
                head += (char)c.read();
                if (head.endsWith("\r\n\r\n")) return true;
            } else if (!c.connected()) {
                return false;
            } else {
                delay(2);
            }
        }
        return head.endsWith("\r\n\r\n");
    }

    // Value of header `name` (lower-case) from a raw request head, or "".
    static String headerValue(const String& head, const char* name) {
        String lower = head;
        lower.toLowerCase();
        int i = lower.indexOf(String(name) + ":");
        if (i < 0) return "";
        i += strlen(name) + 1;
        int end = head.indexOf("\r\n", i);
        String v = (end < 0) ? head.substring(i) : head.substring(i, end);
        v.trim();
        return v;
    }

    void run() override {
        // Staging buffer in PSRAM: the producer's latest frame, copied once per
        // loop and then fanned to each client's own buffer. Off the task stack
        // and out of scarce internal DRAM (a JPEG can be ~150KB).
        uint8_t* shared = (uint8_t*)heap_caps_malloc(FRAME_CAP, MALLOC_CAP_SPIRAM);

        std::list<Client> clients;
        uint32_t sharedSeq = 0;     // producer seq currently in `shared`
        size_t   sharedLen = 0;
        bool     haveShared = false;

        for (;;) {
            // Admit a newly-connected viewer (non-blocking), if we have room.
            NetworkClient inc = accept();
            if (inc) {
                inc.setNoDelay(true);
                // Gate on the device password (HTTP Basic). Reading the request
                // head blocks only this task, never async_tcp or the producer.
                String head;
                if (!readRequestHead(inc, head) ||
                    !PrefEdit::checkBasicAuth(headerValue(head, "authorization"))) {
                    inc.print(F("HTTP/1.1 401 Unauthorized\r\n"
                               "WWW-Authenticate: Basic realm=\"CamCar\"\r\n"
                               "Content-Length: 0\r\n"
                               "Connection: close\r\n\r\n"));
                    inc.stop();
                } else {
                    uint8_t* cb = nullptr;
                    if (shared && (int)clients.size() < MAX_CLIENTS)
                        cb = (uint8_t*)heap_caps_malloc(FRAME_CAP + HDR_CAP, MALLOC_CAP_SPIRAM);
                    if (!cb) {
                        inc.stop();                // OOM or at client cap: refuse
                    } else {
                        inc.print(F("HTTP/1.1 200 OK\r\n"
                                   "Access-Control-Allow-Origin: *\r\n"
                                   "Cache-Control: no-store\r\n"
                                   "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"));
                        clients.push_back(Client{inc, cb, 0, 0, 0});  // seq 0 => next frame
                        mCam.addStreamClient();    // keep the producer running
                    }
                }
            }

            if (clients.empty()) { delay(10); continue; }

            // Refresh the staging frame once for everyone (returns sharedSeq
            // unchanged, and copies nothing, when no newer frame exists).
            uint32_t ns = mCam.copyLatestFrame(shared, FRAME_CAP, sharedLen, sharedSeq);
            if (ns != sharedSeq) { sharedSeq = ns; haveShared = true; }

            bool anyProgress = false;
            for (auto it = clients.begin(); it != clients.end(); ) {
                Client& c = *it;
                if (!c.sock.connected()) {
                    c.sock.stop();
                    heap_caps_free(c.buf);
                    mCam.removeStreamClient();
                    it = clients.erase(it);
                    continue;
                }

                // Done with the current frame? Load the newest staged one,
                // skipping any frames missed while this client was draining.
                if (c.off >= c.len && haveShared && c.seq != sharedSeq) {
                    int hl = snprintf((char*)c.buf, HDR_CAP,
                        "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                        (unsigned)sharedLen);
                    memcpy(c.buf + hl, shared, sharedLen);
                    c.len = (size_t)hl + sharedLen;
                    c.off = 0;
                    c.seq = sharedSeq;
                }

                // Push whatever the socket will take right now. MSG_DONTWAIT
                // makes each send non-blocking regardless of socket mode, so a
                // slow client can't stall the task; it returns a short count
                // (partial write resumes next loop) or EAGAIN when the send
                // buffer is full. A real error (peer reset/closed) drops it.
                if (c.off < c.len) {
                    int n = ::send(c.sock.fd(), c.buf + c.off, c.len - c.off, MSG_DONTWAIT);
                    if (n > 0) {
                        c.off += (size_t)n;
                        anyProgress = true;
                    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        c.sock.stop();             // reaped by the connected() check
                    }
                }
                ++it;
            }

            if (!anyProgress) delay(2);     // nothing to do: yield briefly
        }
    }
};

#endif // MJPEG_STREAM_SERVER_H

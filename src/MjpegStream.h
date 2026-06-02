#ifndef MJPEG_STREAM_H
#define MJPEG_STREAM_H

// HTTP-MJPEG (multipart/x-mixed-replace) chunked-response helper for GET /stream.
// Kept in a header (not the .ino) so the Arduino auto-prototype pass doesn't
// emit a prototype referencing MjpegState before the struct is defined.

#include <Arduino.h>
#include "Camera.h"

// Max JPEG we'll relay per /stream connection. Matches the producer's shared
// frame cap (CameraHandler::SHARED_FRAME_CAP); copyLatestFrame() drops frames
// larger than this, so they're equal by construction.
static constexpr size_t MJPEG_FRAME_CAP = 180 * 1024;

// Per-connection filler state. This connection is a *consumer*: it never grabs
// the camera. The single producer (CameraHandler::sendFrame) publishes each
// frame to a shared double buffer; we latch the latest via copyLatestFrame()
// into our own PSRAM buffer and stream it out across as many fill() calls as
// the chunk size requires. `frame` is allocated by the /stream handler.
struct MjpegState {
  uint8_t* frame;     // PSRAM buffer holding the latched JPEG (handler-allocated)
  size_t   frameLen;  // bytes in `frame` (0 = need to latch a new one)
  size_t   pos;       // bytes of the current segment already sent
  bool     inHeader;  // true = emitting the part header, false = the JPEG body
  size_t   headerLen;
  uint32_t lastSeq;   // last shared-frame seq we latched (de-dupes / paces)
  CameraHandler* cam; // frame source (copyLatestFrame); may be null
  char header[96];
};

// Fill up to maxLen bytes of the multipart stream from the producer's frames.
// One frame per call -> output is paced by the producer (no extra grab, no
// network flooding). When no fresh frame is ready it waits briefly (the
// producer publishes at the paced rate) and, failing that, returns
// RESPONSE_TRY_AGAIN to keep the connection open without ending it.
inline size_t mjpegFill(MjpegState* st, uint8_t* buf, size_t maxLen) {
  if (!st->frame || !st->cam) return 0;          // no buffer -> end the response
  size_t out = 0;
  while (out < maxLen) {
    if (st->frameLen == 0) {                      // need to latch a fresh frame
      // Wait (bounded) for a frame newer than what we last sent. The producer
      // paces publication, so this blocks at most ~one frame interval -- the
      // same shape as the old esp_camera_fb_get() grab, minus the second grab.
      size_t got = 0;
      uint32_t seq = st->lastSeq;
      int64_t deadline = esp_timer_get_time() + 1000000;   // 1s safety net
      while (esp_timer_get_time() < deadline) {
        seq = st->cam->copyLatestFrame(st->frame, MJPEG_FRAME_CAP, got, st->lastSeq);
        if (seq != st->lastSeq && got > 0) break;
        delay(2);                                 // yield (feeds WDT, lets TCP run)
      }
      if (seq == st->lastSeq || got == 0) {
        return out > 0 ? out : RESPONSE_TRY_AGAIN; // nothing new -> keep alive
      }
      st->lastSeq = seq;
      st->frameLen = got;
      st->headerLen = snprintf(st->header, sizeof(st->header),
          "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
          (unsigned)got);
      st->pos = 0;
      st->inHeader = true;
    }
    if (st->inHeader) {
      size_t rem = st->headerLen - st->pos;
      size_t n = rem < (maxLen - out) ? rem : (maxLen - out);
      memcpy(buf + out, st->header + st->pos, n);
      st->pos += n; out += n;
      if (st->pos >= st->headerLen) { st->inHeader = false; st->pos = 0; }
    } else {
      size_t rem = st->frameLen - st->pos;
      size_t n = rem < (maxLen - out) ? rem : (maxLen - out);
      memcpy(buf + out, st->frame + st->pos, n);
      st->pos += n; out += n;
      if (st->pos >= st->frameLen) {
        st->frameLen = 0;                         // done -> latch the next frame
        st->pos = 0;
        break;                                    // one frame per fill -> pacing
      }
    }
  }
  return out;
}

#endif // MJPEG_STREAM_H

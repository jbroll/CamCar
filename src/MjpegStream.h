#ifndef MJPEG_STREAM_H
#define MJPEG_STREAM_H

// HTTP-MJPEG (multipart/x-mixed-replace) chunked-response helper for GET /stream.
// Kept in a header (not the .ino) so the Arduino auto-prototype pass doesn't
// emit a prototype referencing MjpegState before the struct is defined.

#include "esp_camera.h"
#include <Arduino.h>
#include "Camera.h"

// Per-connection filler state. While a client streams here this connection owns
// the camera grab (see Camera setHttpStreaming); each grabbed frame is also
// published to `cam` so a concurrent WS viewer sees the same frames.
struct MjpegState {
  camera_fb_t* fb;
  size_t pos;        // bytes of the current segment already sent
  bool inHeader;     // true = emitting the part header, false = the JPEG body
  size_t headerLen;
  CameraHandler* cam;   // for publishSharedFrame (WS fan-out); may be null
  char header[96];
};

// Fill up to maxLen bytes of the multipart stream. Grabs at most one fresh frame
// per call (at the start of a frame), which paces output to the sensor's rate.
// Returns bytes written; 0 ends the response.
inline size_t mjpegFill(MjpegState* st, uint8_t* buf, size_t maxLen) {
  size_t out = 0;
  while (out < maxLen) {
    if (!st->fb) {                          // start of a new frame
      st->fb = esp_camera_fb_get();
      if (!st->fb) return out;              // capture failed -> end the stream
      if (st->cam) st->cam->publishSharedFrame(st->fb->buf, st->fb->len);  // WS fan-out
      st->headerLen = snprintf(st->header, sizeof(st->header),
          "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
          (unsigned)st->fb->len);
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
      size_t rem = st->fb->len - st->pos;
      size_t n = rem < (maxLen - out) ? rem : (maxLen - out);
      memcpy(buf + out, st->fb->buf + st->pos, n);
      st->pos += n; out += n;
      if (st->pos >= st->fb->len) {
        esp_camera_fb_return(st->fb);
        st->fb = nullptr;
        st->pos = 0;
        break;                              // one frame per fill -> natural pacing
      }
    }
  }
  return out;
}

#endif // MJPEG_STREAM_H

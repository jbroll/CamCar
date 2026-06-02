#include "Camera.h"
#include <WiFi.h>

// Auto-tune candidates: 8.0 .. 20.0 MHz in 0.5 steps. Half-MHz resolution so the
// scan finds a clean band even when it's centered on a half-MHz (the band
// positions are per-channel; see the XCLK lesson). mhz(i) = 8.0 + 0.5*i.
static constexpr uint8_t SCAN_COUNT = 25;
static inline float scanFreqMhz(uint8_t i) { return 8.0f + 0.5f * i; }
static constexpr int64_t SCAN_SETTLE_US = 1000000;  // 1.0s settle after each XCLK change
static constexpr int64_t SCAN_WINDOW_US = 2000000;  // 2.0s measurement window

// Resolution ladder (ascending). Auto-adapt moves within [0, ceiling]; the
// manual "Resolution" command sets the ceiling and jumps to it.
static const framesize_t RES_LADDER[] = {
    FRAMESIZE_QVGA,   // 320x240
    FRAMESIZE_CIF,    // 400x296
    FRAMESIZE_VGA,    // 640x480
    FRAMESIZE_SVGA,   // 800x600
    FRAMESIZE_XGA,    // 1024x768
};
static const uint8_t RES_LADDER_COUNT = sizeof(RES_LADDER) / sizeof(RES_LADDER[0]);

// Snapshot ladder (stills): wider range, up to the OV2640 maximum.
static const framesize_t SNAP_LADDER[] = {
    FRAMESIZE_VGA,    // 640x480
    FRAMESIZE_SVGA,   // 800x600
    FRAMESIZE_XGA,    // 1024x768
    FRAMESIZE_SXGA,   // 1280x1024
    FRAMESIZE_UXGA,   // 1600x1200
};
static const uint8_t SNAP_LADDER_COUNT = sizeof(SNAP_LADDER) / sizeof(SNAP_LADDER[0]);

static const char* framesizeName(framesize_t fs) {
    switch (fs) {
        case FRAMESIZE_QVGA: return "QVGA 320x240";
        case FRAMESIZE_CIF:  return "CIF 400x296";
        case FRAMESIZE_VGA:  return "VGA 640x480";
        case FRAMESIZE_SVGA: return "SVGA 800x600";
        case FRAMESIZE_XGA:  return "XGA 1024x768";
        case FRAMESIZE_SXGA: return "SXGA 1280x1024";
        case FRAMESIZE_UXGA: return "UXGA 1600x1200";
        default:             return "?";
    }
}

uint8_t CameraHandler::snapshotCount() { return SNAP_LADDER_COUNT; }

static int ladderIndex(framesize_t fs) {
    for (uint8_t i = 0; i < RES_LADDER_COUNT; i++) {
        if (RES_LADDER[i] == fs) return i;
    }
    return -1;
}

CameraHandler::CameraHandler(AsyncWebSocket& wsCamera)
    : mWsCamera(wsCamera)
    , mClientId(0)
    , mTargetFPS(DEFAULT_FPS)
    , mFrameSize(DEFAULT_FRAMESIZE)
    , mJpegQuality(DEFAULT_JPEG_QUALITY)
    , mXclkFreq(XCLK_FREQ_HZ)
    , mLastFrameTime(0)
    , mCeilingIdx(0)
    , mLevelIdx(0)
    , mSentSlots(0)
    , mDroppedSlots(0)
    , mAutoAdapt(true)
    , mClearWindows(0)
    , mCongestedWindows(0)
    , mLastAdaptTime(0)
    , mUpshiftInhibitUntil(0)
    , mPauseRequested(false)
    , mPaused(false)
    , mHttpStreaming(false)
    , mCameraStopped(false)
    , mSharedIdx(0)
    , mSharedSeq(0)
    , mWsSentSeq(0)
    , mDeliveredFrames(0)
    , mScanning(false)
    , mScanDone(false)
    , mScanMeasuring(false)
    , mScanSavedAdapt(true)
    , mScanIdx(0)
    , mScanPhaseStart(0)
    , mScanMeasureStart(0)
    , mScanFrameMark(0)
    , mScanBestFps(0)
    , mScanBestMhz(8)
{
    mShared[0] = nullptr; mShared[1] = nullptr;
    mSharedLen[0] = 0;    mSharedLen[1] = 0;
    mSharedMux = portMUX_INITIALIZER_UNLOCKED;
    int idx = ladderIndex(DEFAULT_FRAMESIZE);
    mCeilingIdx = (idx >= 0) ? (uint8_t)idx : 0;
    mLevelIdx = mCeilingIdx;
}

void CameraHandler::setClientId(uint32_t id) { mClientId = id; }
uint32_t CameraHandler::getClientId() const { return mClientId; }
void CameraHandler::clearClientId() { mClientId = 0; }
void CameraHandler::setFPS(uint8_t fps) { mTargetFPS = constrain(fps, MIN_FPS, MAX_FPS); }
uint8_t CameraHandler::getFPS() const { return mTargetFPS; }
framesize_t CameraHandler::getResolution() const { return mFrameSize; }

CameraHandler::~CameraHandler() {
    esp_camera_deinit();
}

// (Re-)initialise the camera at mXclkFreq and re-apply all sensor settings.
// Safe to call repeatedly (deinits first) -- used by both begin() and the
// runtime XCLK change.
bool CameraHandler::initSensor() {
    esp_camera_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = mXclkFreq;
    config.pixel_format = PIXFORMAT_JPEG;
    // Size the framebuffer for the largest size we ever use (UXGA snapshots),
    // so switching *up* on the fly always fits; the sensor is set to the
    // current (smaller) streaming size right after init.
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = mJpegQuality;

    // Frame buffer in PSRAM (needed for larger JPEGs). Double-buffer so the
    // sensor can fill one while we transmit the other; sendFrame() returns the
    // buffer immediately and drops frames under backpressure.
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    config.fb_count = psramFound() ? 2 : 1;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, mFrameSize);
        s->set_quality(s, mJpegQuality);
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
        s->set_contrast(s, 0);
        s->set_special_effect(s, 0);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_wb_mode(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)4);
    }

    mLastAdaptTime = esp_timer_get_time();
    Serial.printf("Camera ready: %s q%u  XCLK %lu Hz  Free PSRAM: %u bytes\n",
                  framesizeName(mFrameSize), mJpegQuality,
                  (unsigned long)mXclkFreq, ESP.getFreePsram());
    return true;
}

bool CameraHandler::begin() {
    if (!initSensor()) return false;

    // Double buffer for fanning MJPEG frames out to a concurrent WS viewer
    // (one-time PSRAM alloc; non-fatal if it fails -> fan-out stays disabled).
    mShared[0] = (uint8_t*)ps_malloc(SHARED_FRAME_CAP);
    mShared[1] = (uint8_t*)ps_malloc(SHARED_FRAME_CAP);
    if (!mShared[0] || !mShared[1]) {
        Serial.println("[stream] shared-frame alloc failed; WS+MJPEG fan-out disabled");
    }
    return true;
}

// Runtime XCLK change: re-init the sensor at the new frequency. WiFi is not
// touched, so the board stays reachable -- unless the new XCLK itself swamps
// 2.4 GHz WiFi, in which case recover by reflashing.
bool CameraHandler::setXclkFreq(uint32_t hz) {
    mXclkFreq = hz;
    if (mCameraStopped) {           // record only; applied on next start
        Serial.printf("[xclk] -> %lu Hz (stored; camera stopped)\n", (unsigned long)hz);
        return true;
    }

    // Pause the loop's grab (snapshot handshake) so we don't deinit mid-capture.
    mPauseRequested = true;
    int64_t deadline = esp_timer_get_time() + 300000;  // 300ms safety net
    while (!mPaused && esp_timer_get_time() < deadline) {
        delay(1);
    }

    bool ok = initSensor();

    mPauseRequested = false;
    Serial.printf("[xclk] -> %lu Hz (%s)\n", (unsigned long)hz, ok ? "ok" : "FAILED");
    return ok;
}

// Stop the camera (deinit -> XCLK off -> 2.4 GHz radiation stops) or restart it.
void CameraHandler::setCameraEnabled(bool on) {
    if (on) {
        if (!mCameraStopped) return;
        mCameraStopped = false;
        initSensor();                       // restart at the current mXclkFreq
        Serial.println("[cam] started");
        return;
    }
    if (mCameraStopped) return;

    // Pause the loop's grab, then power down so the XCLK clock stops.
    mPauseRequested = true;
    int64_t deadline = esp_timer_get_time() + 300000;
    while (!mPaused && esp_timer_get_time() < deadline) {
        delay(1);
    }
    esp_camera_deinit();
    mCameraStopped = true;
    mPauseRequested = false;
    Serial.println("[cam] stopped (XCLK off, RF cleared)");
}

// ---- Auto-tune scan (driven by scanTick() from loop(), so XCLK changes are
// sequential with sendFrame -- no pause handshake needed; we deinit/init directly). ----

void CameraHandler::startScan() {
    if (mScanning) return;
    if (mClientId == 0 || mHttpStreaming || mCameraStopped) {
        Serial.println("[scan] need a connected viewer, camera running, no /stream");
        return;
    }
    mScanSavedAdapt = mAutoAdapt;
    mAutoAdapt = false;             // hold resolution fixed across the sweep
    mScanning = true;
    mScanDone = false;
    mScanIdx = 0;
    mScanBestFps = -1.0f;
    mScanBestMhz = scanFreqMhz(0);
    mScanMeasuring = false;
    mScanPhaseStart = 0;            // 0 => apply the current candidate on next tick
    AsyncWebSocketClient* c = mWsCamera.client(mClientId);
    if (c) c->text("scanstart");
    Serial.println("[scan] started");
}

void CameraHandler::scanTick() {
    if (!mScanning) return;
    int64_t now = esp_timer_get_time();

    if (mScanPhaseStart == 0) {     // apply the current candidate (loop context)
        mXclkFreq = (uint32_t)(scanFreqMhz(mScanIdx) * 1000000.0f);
        initSensor();
        mScanPhaseStart = esp_timer_get_time();
        mScanMeasuring = false;
        return;
    }
    if (!mScanMeasuring) {          // settling: don't count yet
        if (now - mScanPhaseStart >= SCAN_SETTLE_US) {
            mScanFrameMark = mDeliveredFrames;
            mScanMeasureStart = now;
            mScanMeasuring = true;
        }
        return;
    }
    if (now - mScanMeasureStart < SCAN_WINDOW_US) return;   // still measuring

    float fps = (mDeliveredFrames - mScanFrameMark) * 1000000.0f / (now - mScanMeasureStart);
    float mhz = scanFreqMhz(mScanIdx);
    AsyncWebSocketClient* c = mWsCamera.client(mClientId);
    if (c) { char b[32]; snprintf(b, sizeof(b), "scan %.1f %.1f", mhz, fps); c->text(b); }
    Serial.printf("[scan] %.1f MHz -> %.1f fps\n", mhz, fps);
    if (fps > mScanBestFps) { mScanBestFps = fps; mScanBestMhz = mhz; }

    if (++mScanIdx >= SCAN_COUNT) { finishScan(); return; }
    mScanPhaseStart = 0;            // trigger apply of the next candidate
}

void CameraHandler::finishScan() {
    mXclkFreq = (uint32_t)(mScanBestMhz * 1000000.0f);
    initSensor();
    mAutoAdapt = mScanSavedAdapt;
    mScanning = false;
    mScanDone = true;               // loop() persists the winner to NVS
    AsyncWebSocketClient* c = mWsCamera.client(mClientId);
    if (c) { char b[32]; snprintf(b, sizeof(b), "scanbest %.1f", mScanBestMhz); c->text(b); }
    Serial.printf("[scan] best: %.1f MHz @ %.1f fps\n", mScanBestMhz, mScanBestFps);
}

bool CameraHandler::consumeScanDone() {
    if (!mScanDone) return false;
    mScanDone = false;
    return true;
}

// Called from the MJPEG producer (async task). Copies the frame into the
// inactive buffer and publishes it for the WS consumer -- but only when a WS
// client is actually connected, so MJPEG-only streaming pays no copy cost.
void CameraHandler::publishSharedFrame(const uint8_t* data, size_t len) {
    if (mClientId == 0) return;                        // no WS consumer
    if (!mShared[0] || !mShared[1]) return;            // alloc failed
    if (len == 0 || len > SHARED_FRAME_CAP) return;    // too big -> skip this frame
    uint8_t w = mSharedIdx ^ 1;                        // write the inactive buffer
    memcpy(mShared[w], data, len);
    portENTER_CRITICAL(&mSharedMux);
    mSharedLen[w] = len;
    mSharedIdx = w;
    mSharedSeq++;
    portEXIT_CRITICAL(&mSharedMux);
}

// Called from loop() (via sendFrame) while MJPEG owns the grab: forward the
// latest published frame to the WS client, with the same backpressure drop.
bool CameraHandler::forwardSharedToWs() {
    if (mClientId == 0) return false;
    AsyncWebSocketClient* client = mWsCamera.client(mClientId);
    if (!client) return false;

    uint8_t idx; size_t len; uint32_t seq;
    portENTER_CRITICAL(&mSharedMux);
    idx = mSharedIdx; seq = mSharedSeq; len = mSharedLen[idx];
    portEXIT_CRITICAL(&mSharedMux);

    if (len == 0 || seq == mWsSentSeq) return false;   // nothing new yet
    if (client->queueLen() > MAX_INFLIGHT_FRAMES) {    // backpressure: skip, stay current
        mWsSentSeq = seq;
        return false;
    }
    mWsSentSeq = seq;
    return mWsCamera.binary(mClientId, mShared[idx], len);
}

bool CameraHandler::applyLevel() {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    framesize_t fs = RES_LADDER[mLevelIdx];
    if (s->set_framesize(s, fs) != 0) return false;
    mFrameSize = fs;
    return true;
}

// Manual selection: set the ceiling (max resolution) by ladder index, jump to it.
bool CameraHandler::setResolution(uint8_t ladderIndex) {
    if (ladderIndex >= RES_LADDER_COUNT) {
        Serial.printf("setResolution: index %u out of range (max %u)\n",
                      ladderIndex, RES_LADDER_COUNT - 1);
        return false;
    }
    mCeilingIdx = ladderIndex;
    mLevelIdx = ladderIndex;
    mClearWindows = 0;
    if (!applyLevel()) return false;
    Serial.printf("Resolution ceiling -> %s\n", framesizeName(mFrameSize));
    return true;
}

// Manual JPEG quality. esp_camera scale is 4..63 where LOWER is sharper (and
// uses more bandwidth). Applied live; auto-adapt never touches quality.
bool CameraHandler::setQuality(uint8_t q) {
    if (q < 4)  q = 4;
    if (q > 63) q = 63;
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    if (s->set_quality(s, q) != 0) return false;
    mJpegQuality = q;
    Serial.printf("JPEG quality -> q%u\n", q);
    return true;
}

camera_fb_t* CameraHandler::captureSnapshot(uint8_t snapIndex) {
    if (snapIndex >= SNAP_LADDER_COUNT) snapIndex = SNAP_LADDER_COUNT - 1;
    framesize_t res = SNAP_LADDER[snapIndex];

    sensor_t* s = esp_camera_sensor_get();
    if (!s) return nullptr;

    // Pause streaming and wait for the loop's sendFrame() to release the camera.
    mPauseRequested = true;
    int64_t deadline = esp_timer_get_time() + 300000;  // 300ms safety net
    while (!mPaused && esp_timer_get_time() < deadline) {
        delay(1);
    }

    framesize_t prevSize = mFrameSize;
    uint8_t prevQuality = mJpegQuality;
    s->set_quality(s, 10);          // higher quality for a still
    s->set_framesize(s, res);

    // Discard a couple of frames so the new resolution/exposure settles.
    for (int i = 0; i < 2; i++) {
        camera_fb_t* f = esp_camera_fb_get();
        if (f) esp_camera_fb_return(f);
    }
    camera_fb_t* fb = esp_camera_fb_get();

    // Restore streaming settings and resume the stream.
    s->set_framesize(s, prevSize);
    s->set_quality(s, prevQuality);
    mPauseRequested = false;

    Serial.printf("Snapshot %s -> %u bytes\n", framesizeName(res), fb ? fb->len : 0);
    return fb;
}

void CameraHandler::adaptAndReport(int64_t now, AsyncWebSocketClient* client) {
    if (now - mLastAdaptTime < ADAPT_INTERVAL_US) return;
    int64_t elapsed = now - mLastAdaptTime;
    mLastAdaptTime = now;

    uint32_t sent = mSentSlots;
    uint32_t dropped = mDroppedSlots;
    mSentSlots = 0;
    mDroppedSlots = 0;
    uint32_t total = sent + dropped;

    // Auto-adapt between the floor and the user-selected ceiling, with
    // hysteresis to avoid oscillation: downshift only after *sustained*
    // congestion, and suppress upshifts for a cooldown after a downshift.
    // Skipped entirely when locked (mAutoAdapt false) -- resolution stays put.
    if (mAutoAdapt && total > 0) {
        bool congested = (dropped * 2 > total);   // >50% of slots dropped
        if (congested) {
            mClearWindows = 0;
            if (++mCongestedWindows >= DOWNSHIFT_WINDOWS && mLevelIdx > 0) {
                mLevelIdx--;
                applyLevel();
                mCongestedWindows = 0;
                mUpshiftInhibitUntil = now + UPSHIFT_INHIBIT_US;
                Serial.printf("[adapt] congested (%u/%u dropped) -> down to %s\n",
                              dropped, total, framesizeName(mFrameSize));
            }
        } else {
            mCongestedWindows = 0;
            if (dropped == 0 && mLevelIdx < mCeilingIdx && now >= mUpshiftInhibitUntil) {
                if (++mClearWindows >= UPSHIFT_WINDOWS) {
                    mLevelIdx++;
                    applyLevel();
                    mClearWindows = 0;
                    Serial.printf("[adapt] clear -> up to %s\n", framesizeName(mFrameSize));
                }
            } else {
                mClearWindows = 0;
            }
        }
    }

    float fps = sent * 1000000.0f / elapsed;
    Serial.printf("[stream] %.1f fps | %s q%u target %ufps | dropped %u | queue %u | RSSI %lddBm\n",
                  fps, framesizeName(mFrameSize), mJpegQuality, mTargetFPS,
                  dropped, client->queueLen(), (long)WiFi.RSSI());

    // Push device uptime + current XCLK to the page as text frames (the browser
    // tells these from binary JPEG frames by type). The XCLK frame keeps the UI
    // dropdown in sync with the persisted/applied value after a reboot.
    char status[24];
    snprintf(status, sizeof(status), "up %lu", (unsigned long)(now / 1000000));
    client->text(status);
    snprintf(status, sizeof(status), "xclk %.1f", mXclkFreq / 1000000.0f);
    client->text(status);
}

bool CameraHandler::sendFrame() {
    // Yield the camera while a snapshot is in progress (see captureSnapshot).
    if (mPauseRequested) {
        mPaused = true;
        return false;
    }
    mPaused = false;

    if (mCameraStopped) {
        return false;                        // camera deinited (XCLK off)
    }

    // HTTP-MJPEG (/stream) owns the camera grab; instead of grabbing, forward
    // its published frames to the WS client so both viewers see the same video.
    if (mHttpStreaming) {
        return forwardSharedToWs();
    }

    if (mClientId == 0) {
        return false;
    }

    AsyncWebSocketClient* client = mWsCamera.client(mClientId);
    if (!client) {
        return false;
    }

    // Frame-rate pacing. Advance the clock for this slot whether we send or
    // drop, so dropped slots are paced and counted at the frame rate.
    int64_t now = esp_timer_get_time();
    if (now - mLastFrameTime < 1000000 / mTargetFPS) {
        return false;
    }
    mLastFrameTime = now;

    // Backpressure: if the send queue is backing up, drop this slot (keeps
    // latency low) and record it as the congestion signal for auto-adapt.
    if (client->queueLen() > MAX_INFLIGHT_FRAMES) {
        mDroppedSlots++;
        adaptAndReport(now, client);
        return false;
    }

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }
    if (fb->len == 0 || fb->buf == nullptr) {
        esp_camera_fb_return(fb);
        return false;
    }

    // Send, then return the buffer immediately: AsyncWebSocket::binary() copies
    // the data into its own queued message, so we must not hold the camera buffer.
    bool ok = mWsCamera.binary(mClientId, fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (!ok) {
        return false;
    }

    mSentSlots++;
    mDeliveredFrames++;          // free-running; the auto-tune scan metric
    adaptAndReport(now, client);
    return true;
}

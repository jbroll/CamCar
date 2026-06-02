#include "Camera.h"
#include <WiFi.h>

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
    , mLastFrameTime(0)
    , mCeilingIdx(0)
    , mLevelIdx(0)
    , mSentSlots(0)
    , mDroppedSlots(0)
    , mClearWindows(0)
    , mCongestedWindows(0)
    , mLastAdaptTime(0)
    , mUpshiftInhibitUntil(0)
    , mPauseRequested(false)
    , mPaused(false)
{
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

bool CameraHandler::begin() {
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
    config.xclk_freq_hz = XCLK_FREQ_HZ;
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
    Serial.printf("Camera ready: %s q%u  Free PSRAM: %u bytes\n",
                  framesizeName(mFrameSize), mJpegQuality, ESP.getFreePsram());
    return true;
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
    if (total > 0) {
        bool congested = (dropped * 4 > total);   // >25% of slots dropped
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
}

bool CameraHandler::sendFrame() {
    // Yield the camera while a snapshot is in progress (see captureSnapshot).
    if (mPauseRequested) {
        mPaused = true;
        return false;
    }
    mPaused = false;

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
    adaptAndReport(now, client);
    return true;
}

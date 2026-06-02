#include "Camera.h"
#include <WiFi.h>

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

CameraHandler::CameraHandler(AsyncWebSocket& wsCamera)
    : mWsCamera(wsCamera)
    , mClientId(0)
    , mTargetFPS(DEFAULT_FPS)
    , mFrameSize(DEFAULT_FRAMESIZE)
    , mJpegQuality(DEFAULT_JPEG_QUALITY)
    , mLastFrameTime(0)
{
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
    config.frame_size = mFrameSize;
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

    Serial.printf("Camera ready: %s q%u  Free PSRAM: %u bytes\n",
                  framesizeName(mFrameSize), mJpegQuality, ESP.getFreePsram());
    return true;
}

bool CameraHandler::setResolution(framesize_t frameSize) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("setResolution: no sensor");
        return false;
    }
    if (s->set_framesize(s, frameSize) != 0) {
        Serial.printf("setResolution: failed to set framesize %d\n", (int)frameSize);
        return false;
    }
    mFrameSize = frameSize;
    Serial.printf("Resolution -> %s\n", framesizeName(frameSize));
    return true;
}

bool CameraHandler::sendFrame() {
    if (mClientId == 0) {
        return false;
    }

    AsyncWebSocketClient* client = mWsCamera.client(mClientId);
    if (!client) {
        return false;
    }

    // Frame-rate pacing
    int64_t now = esp_timer_get_time();
    if (now - mLastFrameTime < 1000000 / mTargetFPS) {
        return false;
    }

    // Backpressure: keep the send queue shallow so latency stays low. If it's
    // backing up, drop this frame (stream the newest, don't buffer stale ones).
    if (client->queueLen() > MAX_INFLIGHT_FRAMES) {
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
    mLastFrameTime = now;

    // Periodic stream report (visible via `make monitor`): actual fps + mode.
    static uint32_t framesSent = 0;
    static int64_t lastReport = 0;
    framesSent++;
    if (now - lastReport >= 2000000) {  // every 2s
        float fps = framesSent * 1000000.0f / (now - lastReport);
        Serial.printf("[stream] %.1f fps | %s q%u target %ufps | queue %u | RSSI %lddBm\n",
                      fps, framesizeName(mFrameSize), mJpegQuality, mTargetFPS,
                      client->queueLen(), (long)WiFi.RSSI());
        framesSent = 0;
        lastReport = now;
    }
    return true;
}

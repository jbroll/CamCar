#include "Camera.h"

CameraHandler::CameraHandler(AsyncWebSocket& wsCamera)
    : mWsCamera(wsCamera)
    , mClientId(0)
    , mPriorFrame(nullptr)
    , mTargetFPS(15)  // Default to 15 FPS
    , mLastFrameTime(0)
{}

bool CameraHandler::begin() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_4;
    config.ledc_timer = LEDC_TIMER_2;
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
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;  // Enable double buffering

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    if (psramFound()) {
        heap_caps_malloc_extmem_enable(20000);
        Serial.println("PSRAM initialized for camera buffering");
    }

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, FRAMESIZE_VGA);
        s->set_quality(s, 10);
    }

    return true;
}

void CameraHandler::setFPS(uint8_t fps) {
    // Constrain FPS to valid range
    mTargetFPS = constrain(fps, MIN_FPS, MAX_FPS);
}

bool CameraHandler::sendFrame() {
    if (mClientId == 0) {
        if (mPriorFrame) {
            esp_camera_fb_return(mPriorFrame);
            mPriorFrame = nullptr;
        }
        return false;
    }

    unsigned long currentTime = millis();
    unsigned long frameInterval = 1000 / mTargetFPS;
    
    if (currentTime - mLastFrameTime < frameInterval) {
        return false;  // Too soon for next frame
    }

    unsigned long startTime = currentTime;

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        return false;
    }

    if (mPriorFrame) {
        mWsCamera.binary(mClientId, mPriorFrame->buf, mPriorFrame->len);
        
        while (true) {
            AsyncWebSocketClient* clientPointer = mWsCamera.client(mClientId);
            if (!clientPointer || !(clientPointer->queueIsFull())) {
                break;
            }
            delay(1);
        }
        
        esp_camera_fb_return(mPriorFrame);
    }

    mPriorFrame = fb;
    mLastFrameTime = currentTime;

    unsigned long duration = millis() - startTime;
    if (duration > frameInterval) {
        Serial.printf("Warning: Frame processing time (%lu ms) exceeds frame interval (%lu ms)\n", 
                     duration, frameInterval);
    }

    return true;
}

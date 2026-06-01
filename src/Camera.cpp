#include "Camera.h"
#include "CameraQuality.h"
#include "CameraParams.h"

CameraHandler::CameraHandler(AsyncWebSocket& wsCamera)
    : mWsCamera(wsCamera)
    , mClientId(0)
    , mPriorFrame(nullptr)
    , mTargetFPS(10)
    , mLastFrameTime(0)
    , mCurrentQualityLevel(INITIAL_QUALITY_LEVEL)
    , mQualityChangeTime(0)
    , mTransmissionInProgress(false)
    , mCurrentFrameSize(0)
    , mCaptureFailCount(0)
    , mLastCaptureFailTime(0)
    , mLastCongestionCheck(0)
{
}

void CameraHandler::setClientId(uint32_t id) { 
    mClientId = id; 
}

uint32_t CameraHandler::getClientId() const { 
    return mClientId; 
}

void CameraHandler::clearClientId() { 
    mClientId = 0; 
}

void CameraHandler::setFPS(uint8_t fps) { 
    mTargetFPS = constrain(fps, MIN_FPS, MAX_FPS); 
}

uint8_t CameraHandler::getFPS() const { 
    return mTargetFPS; 
}

CameraHandler::~CameraHandler() {
    cleanupFrame();
}

bool CameraHandler::begin() {
    // First, cleanup any existing camera state
    esp_camera_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure camera
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
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;

    const StreamParameters& params = QUALITY_LEVELS[mCurrentQualityLevel];
    config.frame_size = params.frameSize;
    config.jpeg_quality = params.jpegQuality;
    config.fb_count = params.fbCount;

    // Place the frame buffer in PSRAM (8MB on this board); required for VGA
    // JPEG. fb_location/grab_mode were previously left uninitialized.
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

    Serial.printf("Pre-init - Free Heap: %u bytes, Free PSRAM: %u bytes\n",
                 ESP.getFreeHeap(), ESP.getFreePsram());

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
        s->set_framesize(s, params.frameSize);
        s->set_quality(s, params.jpegQuality);
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

    setFPS(params.targetFPS);
    mQualityChangeTime = esp_timer_get_time();
    resetCaptureStats();

    Serial.printf("Post-init - Free Heap: %u bytes, Free PSRAM: %u bytes\n",
                 ESP.getFreeHeap(), ESP.getFreePsram());

    return true;
}

void CameraHandler::checkCongestion(AsyncWebSocketClient* client) {
    int64_t now = esp_timer_get_time();
    
    // Check congestion every 250ms
    static const int64_t CHECK_INTERVAL = 250000;
    if (now - mLastCongestionCheck < CHECK_INTERVAL) {
        return;
    }
    mLastCongestionCheck = now;

    // Get current queue depth
    uint32_t queueDepth = client->queueLen();

    // Define thresholds for congestion detection
    static const uint32_t CONGESTION_THRESHOLD = 12;  // Number of buffers that indicates congestion
    static const uint32_t UPGRADE_THRESHOLD = 4;     // Low queue suggests we can try higher quality
    
    // Only consider quality changes after stability period
    if (now - mQualityChangeTime < QUALITY_STABILITY_PERIOD_MICROS) {
        return;
    }

    if (queueDepth > CONGESTION_THRESHOLD && mCurrentQualityLevel > 0) {
        // Queue building up - reduce quality
        uint8_t newLevel = mCurrentQualityLevel - 1;
        if (applyQualityLevel(newLevel)) {
            Serial.printf("Congestion detected (queue=%u) - reducing quality: %d -> %d\n",
                         queueDepth, mCurrentQualityLevel, newLevel);
            mCurrentQualityLevel = newLevel;
            mQualityChangeTime = now;
        }
    } else if (queueDepth < UPGRADE_THRESHOLD && 
               mCurrentQualityLevel < (QUALITY_LEVELS_COUNT - 1) &&
               mCaptureFailCount == 0 &&
               (now - mQualityChangeTime) >= UPGRADE_STABILITY_PERIOD_MICROS) {
        // Queue staying clear - try increasing quality
        uint8_t newLevel = mCurrentQualityLevel + 1;
        if (applyQualityLevel(newLevel)) {
            Serial.printf("Network clear (queue=%u) - increasing quality: %d -> %d\n",
                         queueDepth, mCurrentQualityLevel, newLevel);
            mCurrentQualityLevel = newLevel;
            mQualityChangeTime = now;
        }
    }
}

bool CameraHandler::applyQualityLevel(uint8_t level) {
    if (!isValidQualityLevel(level)) {
        Serial.printf("ERROR: Attempted to apply invalid quality level %d\n", level);
        return false;
    }

    const StreamParameters& params = QUALITY_LEVELS[level];
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Failed to get sensor interface");
        return false;
    }

    esp_err_t err = ESP_OK;
    err |= s->set_framesize(s, params.frameSize);
    err |= s->set_quality(s, params.jpegQuality);

    if (err != ESP_OK) {
        Serial.printf("Failed to update camera parameters (error 0x%x)\n", err);
        return false;
    }

    setFPS(params.targetFPS);
    return true;
}

void CameraHandler::cleanupFrame() {
    if (mPriorFrame) {
        esp_camera_fb_return(mPriorFrame);
        mPriorFrame = nullptr;
    }
}

void CameraHandler::resetCaptureStats() {
    mCaptureFailCount = 0;
    mLastCaptureFailTime = 0;
}

bool CameraHandler::handleCaptureFailure() {
    int64_t now = esp_timer_get_time();
    mLastCaptureFailTime = now;
    mCaptureFailCount++;

    if (mCaptureFailCount >= MAX_CAPTURE_RETRIES && mCurrentQualityLevel > 0) {
        uint8_t newLevel = mCurrentQualityLevel - 1;
        if (applyQualityLevel(newLevel)) {
            Serial.printf("Downgrading quality level due to capture failures: %d -> %d\n",
                         mCurrentQualityLevel, newLevel);
            mCurrentQualityLevel = newLevel;
            mQualityChangeTime = now;
            resetCaptureStats();
            return true;
        }
    }
    return false;
}

bool CameraHandler::sendFrame() {
    if (mClientId == 0) {
        cleanupFrame();
        mTransmissionInProgress = false;
        return false;
    }

    AsyncWebSocketClient* client = mWsCamera.client(mClientId);
    if (!client) {
        Serial.println("Client disconnected");
        cleanupFrame();
        mTransmissionInProgress = false;
        return false;
    }

    // Check congestion and adjust quality if needed
    checkCongestion(client);

    // If transmission is in progress, check its status
    if (mTransmissionInProgress) {
        if (client->queueLen() <= 2) {  // Nearly empty queue
            mTransmissionInProgress = false;
            cleanupFrame();
        }
        return false;
    }

    // Check frame rate timing
    int64_t now = esp_timer_get_time();
    int64_t frameInterval = 1000000 / mTargetFPS;
    if (now - mLastFrameTime < frameInterval) {
        return false;
    }

    // Capture new frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        if (handleCaptureFailure()) {
            Serial.println("Quality downgraded due to capture failure");
        }
        return false;
    }

    // Validate frame
    if (fb->len == 0 || fb->buf == nullptr) {
        Serial.println("Invalid frame data");
        esp_camera_fb_return(fb);
        return false;
    }

    // Check client queue
    if (client->queueIsFull()) {
        Serial.println("Client queue full, skipping frame");
        esp_camera_fb_return(fb);
        return false;
    }

    // Send frame
    if (!mWsCamera.binary(mClientId, fb->buf, fb->len)) {
        Serial.println("Failed to send frame");
        esp_camera_fb_return(fb);
        return false;
    }

    // Update state
    mPriorFrame = fb;
    mLastFrameTime = now;
    mTransmissionInProgress = true;

    return true;
}

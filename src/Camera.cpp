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
    , mTransmitStartTime(0)
    , mCurrentFrameSize(0)
    , mCaptureFailCount(0)
    , mLastCaptureFailTime(0)
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
    config.ledc_channel = LEDC_CHANNEL_0;  // Changed from 4 to avoid conflicts
    config.ledc_timer = LEDC_TIMER_0;      // Changed from 2 to match channel
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

    static constexpr uint8_t INITIAL_QUALITY_LEVEL =
        (QUALITY_LEVELS_COUNT > 0) ? (QUALITY_LEVELS_COUNT - 1) / 2 : 0;

    const StreamParameters& params = QUALITY_LEVELS[INITIAL_QUALITY_LEVEL];
    config.frame_size = params.frameSize;
    config.jpeg_quality = params.jpegQuality;
    config.fb_count = params.fbCount;

    // Log memory state before init
    Serial.printf("Pre-init - Free Heap: %u bytes, Free PSRAM: %u bytes\n",
                 ESP.getFreeHeap(), ESP.getFreePsram());

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

    if (psramFound()) {
        heap_caps_malloc_extmem_enable(20000);
        Serial.println("PSRAM initialized for camera buffering");
    }

    // Configure sensor settings
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_framesize(s, params.frameSize);
        s->set_quality(s, params.jpegQuality);

        // Basic settings
        s->set_brightness(s, 1);
        s->set_saturation(s, 0);
        s->set_contrast(s, 0);
        s->set_special_effect(s, 0);

        // Auto settings
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
    mQualityChangeTime = millis();
    resetCaptureStats();

    // Log final memory state
    Serial.printf("Post-init - Free Heap: %u bytes, Free PSRAM: %u bytes\n",
                 ESP.getFreeHeap(), ESP.getFreePsram());

    return true;
}

float CameraHandler::calculateNetworkSpeed(unsigned long transmitTime, size_t frameSize) {
    if (transmitTime == 0 || frameSize == 0) return 0;

    // Convert to KB/s, ensuring floating point division
    float kilobytes = frameSize / 1024.0f;
    float seconds = transmitTime / 1000.0f;
    float rate = kilobytes / seconds;

    return rate;
}

bool CameraHandler::sendFrame() {
    unsigned long now = millis();

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

    // If a transmission was in progress, check if it's complete
    if (mTransmissionInProgress) {
        if (client->queueIsFull()) {
            return false;
        }

        // Previous transmission complete
        unsigned long transmitTime = millis() - mTransmitStartTime;

        float kBps = calculateNetworkSpeed(transmitTime, mCurrentFrameSize);

        mTransmissionInProgress = false;
        updateStreamQuality(kBps);  // Now safe to update quality
        cleanupFrame();
    }

    // Check frame rate timing
    unsigned long frameInterval = 1000 / mTargetFPS;
    if (now - mLastFrameTime < frameInterval) {
        return false;
    }

    // Capture new frame
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        handleCaptureFailure();
        return false;
    }

    // Send the frame
    mTransmitStartTime = now;
    mCurrentFrameSize = fb->len;
    mWsCamera.binary(mClientId, fb->buf, fb->len);

    mPriorFrame = fb;
    mLastFrameTime = now;
    mTransmissionInProgress = true;  // Set after everything else is ready

    return true;
}

void CameraHandler::updateStreamQuality(float measuredKBps) {
    unsigned long now = millis();

    if (now - mQualityChangeTime < QUALITY_STABILITY_PERIOD) {
        return;
    }

    const StreamParameters& currentParams = QUALITY_LEVELS[mCurrentQualityLevel];
    uint8_t newLevel = mCurrentQualityLevel;

    // Check for upgrade possibility first
    if (now - mQualityChangeTime >= UPGRADE_STABILITY_PERIOD &&
        mCurrentQualityLevel < (QUALITY_LEVELS_COUNT - 1) &&
        mCaptureFailCount == 0) {

        const StreamParameters& nextParams = QUALITY_LEVELS[mCurrentQualityLevel + 1];
        float requiredSpeed = nextParams.minKBps * NETWORK_MARGIN;

        if (measuredKBps > requiredSpeed) {
            newLevel = mCurrentQualityLevel + 1;
        }
    }

    // Check for downgrade if no upgrade
    if (newLevel == mCurrentQualityLevel && measuredKBps < currentParams.minKBps) {
        if (mCurrentQualityLevel > 0) {
            newLevel = mCurrentQualityLevel - 1;
        }
    }

    if (newLevel != mCurrentQualityLevel) {
        const StreamParameters& params = QUALITY_LEVELS[newLevel];
        Serial.printf("Quality updated - kBps %f Size: %d, Quality: %d, FPS: %d\n",
                     measuredKBps, (int)params.frameSize, params.jpegQuality, params.targetFPS);

        if (applyQualityLevel(newLevel)) {
            mCurrentQualityLevel = newLevel;
            mQualityChangeTime = now;
        } else {
            Serial.println("Quality change failed!");
        }
    }
}

// Complete applyQualityLevel method
bool CameraHandler::applyQualityLevel(uint8_t level) {
    // Validate level before proceeding
    if (!isValidQualityLevel(level)) {
        Serial.printf("ERROR: Attempted to apply invalid quality level %d\n", level);
        return false;
    }

    const StreamParameters& params = QUALITY_LEVELS[level];

    // Get sensor interface
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
        Serial.println("Failed to get sensor interface");
        return false;
    }

    // Update camera parameters
    esp_err_t err = ESP_OK;
    err |= s->set_framesize(s, params.frameSize);
    err |= s->set_quality(s, params.jpegQuality);

    if (err != ESP_OK) {
        Serial.printf("Failed to update camera parameters (error 0x%x)\n", err);
        return false;
    }

    // Update FPS target
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
    unsigned long now = millis();
    mCaptureFailCount++;
    mLastCaptureFailTime = now;

    // If we're having persistent failures, downgrade quality
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


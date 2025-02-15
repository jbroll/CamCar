#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include "esp_camera.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "esp_timer.h"
#include "esp_camera.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "CameraParams.h"  // Include this first for StreamParameters definition

class CameraHandler {
public:
    static constexpr uint8_t MAX_FPS = 30;
    static constexpr uint8_t MIN_FPS = 1;

    // Forward declare the array but don't try to compute its size here
    static const StreamParameters QUALITY_LEVELS[];
    
    // Use externally defined count instead
    static constexpr uint8_t INITIAL_QUALITY_LEVEL = 7;  // Start at middle VGA quality

    explicit CameraHandler(AsyncWebSocket& wsCamera);
    ~CameraHandler();
    
    bool begin();
    void setClientId(uint32_t id);
    uint32_t getClientId() const;
    void clearClientId();
    void setFPS(uint8_t fps);
    uint8_t getFPS() const;
    bool sendFrame();
    void cleanupFrame();

private:
    // Camera GPIO Configuration
    static constexpr int PWDN_GPIO_NUM = 32;
    static constexpr int RESET_GPIO_NUM = -1;
    static constexpr int XCLK_GPIO_NUM = 0;
    static constexpr int SIOD_GPIO_NUM = 26;
    static constexpr int SIOC_GPIO_NUM = 27;
    static constexpr int Y9_GPIO_NUM = 35;
    static constexpr int Y8_GPIO_NUM = 34;
    static constexpr int Y7_GPIO_NUM = 39;
    static constexpr int Y6_GPIO_NUM = 36;
    static constexpr int Y5_GPIO_NUM = 21;
    static constexpr int Y4_GPIO_NUM = 19;
    static constexpr int Y3_GPIO_NUM = 18;
    static constexpr int Y2_GPIO_NUM = 5;
    static constexpr int VSYNC_GPIO_NUM = 25;
    static constexpr int HREF_GPIO_NUM = 23;
    static constexpr int PCLK_GPIO_NUM = 22;

    // Quality control parameters
    static constexpr unsigned long QUALITY_STABILITY_PERIOD = 5000; // 20000;
    static constexpr unsigned long UPGRADE_STABILITY_PERIOD = 10000; // 40000;
    static constexpr float NETWORK_MARGIN = 1.1f; // 2.00f; 
    static constexpr uint8_t MAX_CAPTURE_RETRIES = 5; 
    static constexpr unsigned long CAPTURE_RETRY_DELAY = 250;

    // Member variables
    AsyncWebSocket& mWsCamera;
    uint32_t mClientId;
    camera_fb_t* mPriorFrame;
    uint8_t mTargetFPS;
    unsigned long mLastFrameTime;
    uint8_t mCurrentQualityLevel;
    unsigned long mQualityChangeTime;
    bool mTransmissionInProgress;
    unsigned long mTransmitStartTime;
    size_t mCurrentFrameSize;
    uint8_t mCaptureFailCount;
    unsigned long mLastCaptureFailTime;

    // Quality management methods
    void updateStreamQuality(float measuredKBps);
    bool applyQualityLevel(uint8_t level);
    float calculateNetworkSpeed(unsigned long transmitTime, size_t frameSize);
    bool handleCaptureFailure();
    void resetCaptureStats();
    
    // Bounds checking helper
    bool isValidQualityLevel(uint8_t level) const {
        return level < QUALITY_LEVELS_COUNT;
    }
};

#endif // CAMERA_HANDLER_H

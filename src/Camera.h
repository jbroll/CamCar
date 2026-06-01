#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include "esp_camera.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "CameraParams.h"
#include "board_config.h"

class CameraHandler {
public:
    // Public constants
    static constexpr uint8_t MAX_FPS = 30;
    static constexpr uint8_t MIN_FPS = 1;
    static constexpr size_t WS_BUFFER_SIZE = 1460;  // WebSocket frame size
    static constexpr uint8_t INITIAL_QUALITY_LEVEL = 10;  // Start at middle VGA quality
    
    // Public static data - needed by CameraQuality.h
    static const StreamParameters QUALITY_LEVELS[];

    // Constructor/Destructor
    explicit CameraHandler(AsyncWebSocket& wsCamera);
    ~CameraHandler();
    
    // Public methods
    bool begin();
    void setClientId(uint32_t id);
    uint32_t getClientId() const;
    void clearClientId();
    void setFPS(uint8_t fps);
    uint8_t getFPS() const;
    bool sendFrame();
    void cleanupFrame();

private:
    // Camera GPIO Configuration - from board_config.h
    static constexpr int PWDN_GPIO_NUM = CAM_PIN_PWDN;
    static constexpr int RESET_GPIO_NUM = CAM_PIN_RESET;
    static constexpr int XCLK_GPIO_NUM = CAM_PIN_XCLK;
    static constexpr int SIOD_GPIO_NUM = CAM_PIN_SIOD;
    static constexpr int SIOC_GPIO_NUM = CAM_PIN_SIOC;
    static constexpr int Y9_GPIO_NUM = CAM_PIN_D7;
    static constexpr int Y8_GPIO_NUM = CAM_PIN_D6;
    static constexpr int Y7_GPIO_NUM = CAM_PIN_D5;
    static constexpr int Y6_GPIO_NUM = CAM_PIN_D4;
    static constexpr int Y5_GPIO_NUM = CAM_PIN_D3;
    static constexpr int Y4_GPIO_NUM = CAM_PIN_D2;
    static constexpr int Y3_GPIO_NUM = CAM_PIN_D1;
    static constexpr int Y2_GPIO_NUM = CAM_PIN_D0;
    static constexpr int VSYNC_GPIO_NUM = CAM_PIN_VSYNC;
    static constexpr int HREF_GPIO_NUM = CAM_PIN_HREF;
    static constexpr int PCLK_GPIO_NUM = CAM_PIN_PCLK;

    // Quality control parameters - static constants
    static constexpr int64_t QUALITY_STABILITY_PERIOD_MICROS = 5000000;  // 5 seconds
    static constexpr int64_t UPGRADE_STABILITY_PERIOD_MICROS = 10000000; // 10 seconds
    static constexpr uint8_t MAX_CAPTURE_RETRIES = 5;

    // Member variables - WebSocket related
    AsyncWebSocket& mWsCamera;
    uint32_t mClientId;
    bool mTransmissionInProgress;
    size_t mCurrentFrameSize;

    // Member variables - Camera related
    camera_fb_t* mPriorFrame;
    uint8_t mTargetFPS;
    uint8_t mCurrentQualityLevel;
    uint8_t mCaptureFailCount;

    // Member variables - Timing
    int64_t mLastFrameTime;
    int64_t mQualityChangeTime;
    int64_t mLastCaptureFailTime;
    int64_t mLastCongestionCheck;

    // Private methods
    void checkCongestion(AsyncWebSocketClient* client);
    bool applyQualityLevel(uint8_t level);
    bool handleCaptureFailure();
    void resetCaptureStats();
    
    // Helper methods
    bool isValidQualityLevel(uint8_t level) const {
        return level < QUALITY_LEVELS_COUNT;
    }
};

#endif // CAMERA_HANDLER_H
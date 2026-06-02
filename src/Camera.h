#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include "esp_camera.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_config.h"

class CameraHandler {
public:
    // Public constants
    static constexpr uint8_t MAX_FPS = 30;
    static constexpr uint8_t MIN_FPS = 1;
    static constexpr size_t WS_BUFFER_SIZE = 1460;  // WebSocket frame size

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

    // Change capture resolution on the fly (no camera re-init, no glitch).
    bool setResolution(framesize_t frameSize);
    framesize_t getResolution() const;

    bool sendFrame();

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

    // Stream tuning. 8 MHz XCLK is critical: 10/20 MHz radiate into the 2.4 GHz
    // WiFi band and wreck throughput (espressif/arduino-esp32 #5834).
    static constexpr uint32_t XCLK_FREQ_HZ = 8000000;
    static constexpr framesize_t DEFAULT_FRAMESIZE = FRAMESIZE_VGA;
    static constexpr uint8_t DEFAULT_JPEG_QUALITY = 12;
    static constexpr uint8_t DEFAULT_FPS = 15;
    // Max queued frames before we drop one (bounds latency under backpressure).
    static constexpr uint32_t MAX_INFLIGHT_FRAMES = 3;
    static constexpr int64_t ADAPT_INTERVAL_US = 2000000;  // re-evaluate resolution every 2s

    // Apply the current resolution-ladder level to the sensor (no re-init).
    bool applyLevel();
    // Periodic: auto-adjust resolution to the link, then emit the stream report.
    void adaptAndReport(int64_t now, AsyncWebSocketClient* client);

    AsyncWebSocket& mWsCamera;
    uint32_t mClientId;
    uint8_t mTargetFPS;
    framesize_t mFrameSize;
    uint8_t mJpegQuality;
    int64_t mLastFrameTime;

    // Auto-adapt state. Resolution moves on a ladder between 0 and the
    // user-selected ceiling; manual selection sets the ceiling and jumps to it.
    uint8_t mCeilingIdx;     // max resolution (manual selection)
    uint8_t mLevelIdx;       // current resolution (<= ceiling)
    uint32_t mSentSlots;     // frames sent this adapt window
    uint32_t mDroppedSlots;  // frames dropped (backpressure) this adapt window
    uint8_t mClearWindows;   // consecutive clean windows (upshift hysteresis)
    int64_t mLastAdaptTime;
};

#endif // CAMERA_HANDLER_H

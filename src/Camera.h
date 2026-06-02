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

    // Set the resolution ceiling by ladder index (0..count-1) and jump to it.
    // Index-based so the UI is immune to framesize_t enum value shifts between
    // esp_camera versions. No camera re-init / no glitch.
    bool setResolution(uint8_t ladderIndex);
    framesize_t getResolution() const;

    // When disabled, the resolution is locked: auto-adapt no longer steps it
    // up or down (manual setResolution still works).
    void setAdaptEnabled(bool enabled) { mAutoAdapt = enabled; }

    bool sendFrame();

    // Capture one still at SNAP_LADDER[snapIndex] (up to UXGA). Briefly pauses
    // streaming, switches the sensor, grabs a frame, restores, and resumes.
    // Returns a JPEG fb the caller MUST esp_camera_fb_return(); null on failure.
    camera_fb_t* captureSnapshot(uint8_t snapIndex);
    static uint8_t snapshotCount();

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
    static constexpr uint8_t DOWNSHIFT_WINDOWS = 2;        // sustained congested windows before downshift
    static constexpr uint8_t UPSHIFT_WINDOWS = 2;          // clean windows before an upshift
    static constexpr int64_t UPSHIFT_INHIBIT_US = 30000000; // no upshift for 30s after a downshift

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
    bool mAutoAdapt;             // when false the resolution is locked
    uint8_t mClearWindows;       // consecutive clean windows (upshift hysteresis)
    uint8_t mCongestedWindows;   // consecutive congested windows (downshift hysteresis)
    int64_t mLastAdaptTime;
    int64_t mUpshiftInhibitUntil; // suppress upshifts until this time (post-downshift cooldown)

    // Snapshot coordination: captureSnapshot() runs in the async HTTP task and
    // must not touch the camera while the loop's sendFrame() does. It requests
    // a pause and waits for sendFrame() to acknowledge before capturing.
    volatile bool mPauseRequested;
    volatile bool mPaused;
};

#endif // CAMERA_HANDLER_H

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

    // Set JPEG quality (esp_camera scale: 4..63, LOWER = sharper/bigger frame).
    bool setQuality(uint8_t q);

    // Change the camera XCLK at runtime (re-inits the sensor; WiFi untouched).
    // Used to sweep for the highest RF-clean frequency without reflashing.
    // WARNING: a too-high XCLK can swamp 2.4 GHz WiFi (see the XCLK lesson).
    bool setXclkFreq(uint32_t hz);
    uint32_t getXclkFreq() const { return mXclkFreq; }

    // Stop/start the camera. Stopping deinits it, which halts the XCLK clock and
    // so clears its 2.4 GHz radiation -- the recovery path when a chosen XCLK is
    // disturbing WiFi: stop the camera, regain solid control, pick 8 MHz, start.
    void setCameraEnabled(bool on);
    bool isCameraStopped() const { return mCameraStopped; }

    // Auto-tune: sweep candidate XCLKs, score each by delivered fps to the WS
    // client, adopt the best. Driven by scanTick() from loop(); reports progress
    // via "scan <MHz> <fps>" and "scanbest <MHz>" text frames. Needs a connected
    // WS client (the page) to measure delivery.
    void startScan();
    void scanTick();
    bool consumeScanDone();    // true once after a scan completes (caller persists)
    bool isScanning() const { return mScanning; }

    // When disabled, the resolution is locked: auto-adapt no longer steps it
    // up or down (manual setResolution still works).
    void setAdaptEnabled(bool enabled) { mAutoAdapt = enabled; }

    // HTTP-MJPEG (/stream) consumer registration. Each connected /stream client
    // increments this; sendFrame() keeps producing frames while it is > 0 even
    // with no WS viewer. /stream reads frames via copyLatestFrame() (it does not
    // grab the camera itself -- the single producer is sendFrame()).
    void addStreamClient()    { mStreamClients++; }
    void removeStreamClient() { if (mStreamClients > 0) mStreamClients--; }
    int  streamClients() const { return mStreamClients; }

    // Producer side: copy the latest grabbed frame into the shared double buffer
    // so /stream consumers can read it. Called by sendFrame() for every frame.
    void publishSharedFrame(const uint8_t* data, size_t len);

    // Consumer side (the /stream filler, async task): if a frame newer than
    // lastSeq exists, copy it into dst (cap bytes) and return its seq; otherwise
    // return lastSeq unchanged. Cross-core safe (double-buffered).
    uint32_t copyLatestFrame(uint8_t* dst, size_t cap, size_t& outLen, uint32_t lastSeq);

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
    static constexpr uint8_t DEFAULT_FPS = MAX_FPS;  // ceiling; the XCLK clock paces below it
    // Max queued frames before we drop one. Keep shallow: a deep queue means the
    // viewer watches stale frames (latency). 1 = always send the freshest.
    static constexpr uint32_t MAX_INFLIGHT_FRAMES = 1;
    static constexpr int64_t ADAPT_INTERVAL_US = 2000000;  // re-evaluate resolution every 2s
    static constexpr uint8_t DOWNSHIFT_WINDOWS = 3;        // sustained congested windows before downshift (~6s)
    static constexpr uint8_t UPSHIFT_WINDOWS = 2;          // clean windows before an upshift
    static constexpr int64_t UPSHIFT_INHIBIT_US = 30000000; // no upshift for 30s after a downshift

    // (Re-)initialise the camera at mXclkFreq and re-apply sensor settings.
    bool initSensor();
    // Apply the current resolution-ladder level to the sensor (no re-init).
    bool applyLevel();
    // Periodic: auto-adjust resolution to the link, then emit the stream report
    // and push status text frames to all viewers.
    void adaptAndReport(int64_t now);

    AsyncWebSocket& mWsCamera;
    uint32_t mClientId;
    uint8_t mTargetFPS;
    framesize_t mFrameSize;
    uint8_t mJpegQuality;
    uint32_t mXclkFreq;          // current camera XCLK in Hz (default XCLK_FREQ_HZ)
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

    // Count of connected HTTP-MJPEG (/stream) clients. While > 0 the producer
    // keeps grabbing even with no WS viewer; /stream consumes via copyLatestFrame.
    volatile int mStreamClients;

    // True while the camera is stopped (deinited, XCLK off) -- see setCameraEnabled.
    bool mCameraStopped;

    // Frame fan-out (MJPEG producer -> WS consumer). Double-buffered so the
    // async-task writer and the loop() reader can run on different cores: the
    // writer fills the inactive buffer then publishes (index/len/seq) under a
    // short spinlock; the reader copies the published buffer out (binary() is
    // fast, the writer won't lap it within two frame intervals).
    static constexpr size_t SHARED_FRAME_CAP = 180 * 1024;  // ample for up to XGA
    uint8_t* mShared[2];
    size_t   mSharedLen[2];
    volatile uint8_t  mSharedIdx;   // currently published buffer (0/1)
    volatile uint32_t mSharedSeq;   // bumps on each publish
    portMUX_TYPE mSharedMux;

    // Auto-tune scan: free-running counter of successful WS sends (the scan
    // metric -- a dirty XCLK collapses delivered throughput) + the state machine.
    uint32_t mDeliveredFrames;
    bool mScanning;
    bool mScanDone;            // set once when a scan finishes (consumeScanDone)
    bool mScanMeasuring;       // false = settling after an XCLK change, true = counting
    bool mScanSavedAdapt;      // mAutoAdapt to restore after the scan
    uint8_t mScanIdx;          // index into SCAN_FREQS
    int64_t mScanPhaseStart;   // when the current frequency's settle began
    int64_t mScanMeasureStart;
    uint32_t mScanFrameMark;   // mDeliveredFrames at measurement start
    float mScanBestFps;
    float mScanBestMhz;
    uint8_t mScanSavedLevel;   // resolution level to restore after the scan
    void finishScan();
};

#endif // CAMERA_HANDLER_H

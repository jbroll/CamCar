#ifndef CAMERA_HANDLER_H
#define CAMERA_HANDLER_H

#include "esp_camera.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>

class CameraHandler {
public:
    CameraHandler(AsyncWebSocket& wsCamera);
    bool begin();
    void setClientId(uint32_t id) { mClientId = id; }
    uint32_t getClientId() const { return mClientId; }
    void clearClientId() { mClientId = 0; }
    void sendFrame();

private:
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

    AsyncWebSocket& mWsCamera;
    uint32_t mClientId;
    camera_fb_t* mPriorFrame;
};

#endif // CAMERA_HANDLER_H
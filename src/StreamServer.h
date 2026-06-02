#ifndef STREAM_SERVER_H
#define STREAM_SERVER_H

#include <Arduino.h>
#include <WiFiServer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "Camera.h"

// Base for streaming servers fed by the single frame producer
// (CameraHandler::sendFrame). Each instance owns a WiFiServer listening socket
// on its own port plus a dedicated FreeRTOS task running run(). Because the
// pump lives on its own task, blocking socket writes are fine -- they park only
// this task, never AsyncTCP's event loop (the WS control channel) or loop()
// (the producer). Subclasses implement run() with their wire protocol and read
// frames via CameraHandler::copyLatestFrame(); they never grab the camera.
//
// See CLAUDE.md "Camera streaming": the producer publishes each frame into a
// cross-core double buffer, so these consumer tasks are safe on another core.
class StreamServer : public WiFiServer {
public:
    StreamServer(CameraHandler& cam, uint16_t port, const char* name,
                 uint32_t stackSize = 4096, BaseType_t core = 1)
        : WiFiServer(port), mCam(cam), mName(name),
          mStack(stackSize), mCore(core), mTask(nullptr) {}

    // Start listening and spawn the serve task. Call after WiFi is associated.
    void begin() {
        WiFiServer::begin();
        setNoDelay(true);
        xTaskCreatePinnedToCore(taskTrampoline, mName, mStack, this,
                                1 /* same prio as loop, below async_tcp */,
                                &mTask, mCore);
    }

protected:
    // The subclass serve loop. Runs forever on our task.
    virtual void run() = 0;

    CameraHandler& mCam;

private:
    static void taskTrampoline(void* arg) {
        static_cast<StreamServer*>(arg)->run();
        vTaskDelete(nullptr);     // run() is expected to loop forever; defensive
    }

    const char* mName;
    uint32_t    mStack;
    BaseType_t  mCore;
    TaskHandle_t mTask;
};

#endif // STREAM_SERVER_H

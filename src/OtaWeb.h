#ifndef OTAWEB_H
#define OTAWEB_H

#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "PrefEdit.h"
#include "Camera.h"

// Web firmware update: POST a firmware .bin to /update (HTTP Basic auth).
// Streams the body straight into the esp32 Update API, then deferred-reboots so
// the HTTP response flushes first. Requires a dual-OTA partition table.
class OtaWeb {
public:
    static void begin(AsyncWebServer* server, CameraHandler* camera) {
        _camera = camera;
        server->on("/update", HTTP_POST, handleResult, handleUpload);
    }

    // Call from loop(): performs the post-success reboot once the response has
    // had time to flush (same pattern as PrefEdit::loop()).
    static void loop() {
        if (_rebootAt != 0 && millis() >= _rebootAt) {
            ESP.restart();
        }
    }

private:
    static constexpr unsigned long REBOOT_DELAY_MS = 1500;
    static CameraHandler* _camera;
    static unsigned long _rebootAt;

    static bool authed(AsyncWebServerRequest* request) {
        String user = PrefEdit::get("ota_user", "admin");
        String pass = PrefEdit::get("ota_pass", "camcar");
        return request->authenticate(user.c_str(), pass.c_str());
    }

    // Runs after the whole body is received: report success/failure, schedule
    // the reboot on success.
    static void handleResult(AsyncWebServerRequest* request) {
        if (!authed(request)) {
            return request->requestAuthentication();
        }
        bool ok = !Update.hasError();
        AsyncWebServerResponse* resp = request->beginResponse(
            ok ? 200 : 500, "text/plain",
            ok ? String("Update OK -- rebooting") : String(Update.errorString()));
        resp->addHeader("Connection", "close");
        request->send(resp);
        if (ok) {
            _rebootAt = millis() + REBOOT_DELAY_MS;
        }
    }

    // Streams each upload chunk into Update. Auth is checked once at index 0 so
    // an unauthorized client never touches flash (handleResult sends the 401).
    static void handleUpload(AsyncWebServerRequest* request, String filename,
                             size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
            if (!authed(request)) {
                return;
            }
            Serial.printf("[ota] start: %s\n", filename.c_str());
            if (_camera) {
                _camera->setCameraEnabled(false);  // free the producer during flash
            }
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        }
        if (Update.isRunning()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[ota] success: %u bytes\n",
                                  (unsigned)(index + len));
                } else {
                    Update.printError(Serial);
                }
            }
        }
    }
};

CameraHandler* OtaWeb::_camera = nullptr;
unsigned long OtaWeb::_rebootAt = 0;

#endif

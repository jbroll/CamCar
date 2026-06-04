#ifndef PREFEDIT_H
#define PREFEDIT_H

#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "FileSystem.h"

class PrefEdit {
public:
    // Open the NVS-backed config store. Safe to call before the web server
    // exists (e.g. to read WiFi credentials during boot) and idempotent --
    // begin() calls it too if it hasn't run yet.
    static bool initStorage() {
        if (_inited) {
            return true;
        }
        _inited = _prefs.begin("config", false);
        return _inited;
    }

    static bool begin(AsyncWebServer* server, const char* endpoint,
                     const char** paramArray) {

        if (!initStorage()) {
            return false;
        }

        _params = paramArray;
        _server = server;
        _server->on(endpoint, HTTP_POST, handleUpdate);
        FileSystem::registerProcessor(endpoint, processor);

        return true;
    }

    // Read a stored config value (returns def if unset). Public so boot code
    // can read credentials before the server is up; requires initStorage().
    static String get(const char* key, const String& def = "") {
        return _prefs.getString(key, def);
    }

    // Write a stored config value. Public so boot code can seed defaults
    // (e.g. from build-time .env) before the server is up; requires
    // initStorage().
    static void set(const char* key, const String& value) {
        _prefs.putString(key, value);
    }

    static String processor(const String& var) {
        return getValue(var.c_str());
    }

    // Authed via the camcar_auth cookie (browser, incl. WS handshake) or
    // empty-username Basic (curl/make). Shared by the whole port-80 app.
    static bool checkAuth(AsyncWebServerRequest* request) {
        String pass = get("device_pass", "camcar");
        const AsyncWebHeader* cookie = request->getHeader("Cookie");
        if (cookie && authCookie(cookie->value()) == pass) {
            return true;
        }
        return request->authenticate("", pass.c_str());
    }

    static String authCookie(const String& cookies) {
        int i = cookies.indexOf("camcar_auth=");
        if (i < 0) return "";
        i += 12;
        int end = cookies.indexOf(';', i);
        return end < 0 ? cookies.substring(i) : cookies.substring(i, end);
    }

    static void handleUpdate(AsyncWebServerRequest* request) {
        if (!checkAuth(request)) {
            return request->requestAuthentication();
        }
        bool wifiChanged = false;

        // Check each parameter for changes
        for (const char** param = _params; *param != nullptr; param++) {
            if (request->hasParam(*param, true)) {
                const AsyncWebParameter* p = request->getParam(*param, true);
                String newValue = p->value();
                String currentValue = getValue(*param);

                if (newValue != currentValue) {
                    setValue(*param, newValue.c_str());
                    if (strcmp(*param, "ssid") == 0 ||
                        strcmp(*param, "password") == 0) {
                        wifiChanged = true;
                    }
                }
            }
        }

        // The config UI is the in-page hamburger dialog, which saves via fetch
        // and reads a plain-text status. (No standalone page to redirect to.)
        if (wifiChanged) {
            // New WiFi credentials only take effect on reconnect, so reboot.
            // Defer it (see loop()) so this response can flush first.
            _rebootAt = millis() + REBOOT_DELAY_MS;
            request->send(200, "text/plain", "rebooting");
        } else {
            request->send(200, "text/plain", "saved");
        }
    }

    // Call from the main loop(): performs a deferred reboot after WiFi
    // credentials change, giving the HTTP response time to flush.
    static void loop() {
        if (_rebootAt != 0 && millis() >= _rebootAt) {
            ESP.restart();
        }
    }

private:
    static constexpr unsigned long REBOOT_DELAY_MS = 1000;

    static AsyncWebServer* _server;
    static Preferences _prefs;
    static const char** _params;
    static bool _inited;
    static unsigned long _rebootAt;

    static String getValue(const char* param) {
        return get(param);
    }

    static void setValue(const char* param, const char* value) {
        set(param, value);
    }
};

// Initialize static members
AsyncWebServer* PrefEdit::_server = nullptr;
const char** PrefEdit::_params = nullptr;
Preferences PrefEdit::_prefs;
bool PrefEdit::_inited = false;
unsigned long PrefEdit::_rebootAt = 0;

#endif

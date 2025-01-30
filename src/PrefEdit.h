#ifndef PREFEDIT_H
#define PREFEDIT_H

#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "FileSystem.h"

class PrefEdit {
public:
    static bool begin(AsyncWebServer* server, const char* endpoint, 
                     const char** paramArray) {
        
        if (!_prefs.begin("config", false)) {
            return false;
        }

        _params = paramArray;
        _server = server;
        _server->on(endpoint, HTTP_POST, handleUpdate);
        FileSystem::registerProcessor(endpoint, processor);
        
        return true;
    }

    static String processor(const String& var) {
        return getValue(var.c_str());
    }

    static void handleUpdate(AsyncWebServerRequest* request) {
        bool changed = false;
        
        // Check each parameter for changes
        for (const char** param = _params; *param != nullptr; param++) {
            if (request->hasParam(*param, true)) {
                const AsyncWebParameter* p = request->getParam(*param, true);
                String newValue = p->value();
                String currentValue = getValue(*param);
                
                if (newValue != currentValue) {
                    setValue(*param, newValue.c_str());
                    changed = true;
                }
            }
        }
        
        // Redirect back to the configuration page
        request->redirect(request->url());
    }

private:
    static AsyncWebServer* _server;
    static Preferences _prefs;
    static const char** _params;
    
    static String getValue(const char* param) {
        return _prefs.getString(param, "");
    }

    static void setValue(const char* param, const char* value) {
        _prefs.putString(param, value);
    }
};

// Initialize static members
AsyncWebServer* PrefEdit::_server = nullptr;
const char** PrefEdit::_params = nullptr;
Preferences PrefEdit::_prefs;

#endif

// PrefEdit.h
#ifndef PREFEDIT_H
#define PREFEDIT_H

#include <ESPAsyncWebServer.h>
#include <Preferences.h>

class PrefEdit {
public:
    static bool begin(AsyncWebServer* server, const char* endpoint, 
                     const char** paramArray, const char* pageTemplate);
    static String processor(const String& var);
    static void handleUpdate(AsyncWebServerRequest* request);

private:
    static AsyncWebServer* _server;
    static Preferences _prefs;
    static const char** _params;
    static const char* _template;
    
    static String getValue(const char* param);
    static void setValue(const char* param, const char* value);
    static size_t getParamCount();
};

// Initialize static members
AsyncWebServer* PrefEdit::_server = nullptr;
const char** PrefEdit::_params = nullptr;
const char* PrefEdit::_template = nullptr;
Preferences PrefEdit::_prefs;

// PrefEdit.cpp implementation
bool PrefEdit::begin(AsyncWebServer* server, const char* endpoint,
                    const char** paramArray, const char* pageTemplate) {
    _server = server;
    _params = paramArray;
    _template = pageTemplate;
    
    if (!_prefs.begin("config", false)) {
        return false;
    }
    
    // Set up GET handler for the configuration page
    _server->on(endpoint, HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/html", _template, processor);
    });
    
    // Set up POST handler for form submission
    _server->on(endpoint, HTTP_POST, handleUpdate);
    
    return true;
}

String PrefEdit::processor(const String& var) {
    return getValue(var.c_str());
}

void PrefEdit::handleUpdate(AsyncWebServerRequest* request) {
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

String PrefEdit::getValue(const char* param) {
    return _prefs.getString(param, "");
}

void PrefEdit::setValue(const char* param, const char* value) {
    _prefs.putString(param, value);
}

size_t PrefEdit::getParamCount() {
    size_t count = 0;
    for (const char** p = _params; *p != nullptr; p++) {
        count++;
    }
    return count;
}

#endif // PREFEDIT_H

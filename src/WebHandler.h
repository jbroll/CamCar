#ifndef WEBHANDLER_H
#define WEBHANDLER_H

#include <ESPAsyncWebServer.h>
#include "FileSystem.h"
#include "PrefEdit.h"

class WebHandler {
public:
    static constexpr const char* LOGIN_PAGE =
        "<!DOCTYPE html><html><head><meta name=viewport "
        "content=\"width=device-width,initial-scale=1\"><title>CamCar</title>"
        "<style>body{background:#000;color:#0f0;font:16px monospace;display:flex;"
        "height:100vh;margin:0;align-items:center;justify-content:center}"
        "form{display:flex;flex-direction:column;gap:10px;width:min(80vw,280px)}"
        "input,button{background:#000;color:#0f0;border:1px solid #0f0;"
        "padding:10px;font:16px monospace;border-radius:5px}"
        "#e{color:#f55;font-size:13px;min-height:16px}</style></head><body>"
        "<form method=POST action=/login><h2>CamCar</h2>"
        "<input type=password name=password placeholder=Password autofocus>"
        "<button>Unlock</button><span id=e></span></form>"
        "<script>if(location.search.indexOf('e=1')>=0)"
        "document.getElementById('e').textContent='Wrong password';</script>"
        "</body></html>";

    static void begin(AsyncWebServer& server) {
        server.onNotFound([](AsyncWebServerRequest *request) {
            if (!PrefEdit::checkAuth(request)) {
                request->send(200, "text/html", LOGIN_PAGE);
                return;
            }

            String url = request->url();
            const char* path = url.length() > 0 ? url.c_str() : "/";
            const FileEntry* file = FileSystem::findFileEntry(path);
            if (!file) {
                request->send(404, "text/plain", "File not found");
                return;
            }

            ContentProcessor processor = FileSystem::getProcessor(path);
            AsyncWebServerResponse *response = processor
                ? request->beginResponse(200, file->content_type, file->data, file->size, processor)
                : request->beginResponse(200, file->content_type, file->data, file->size);
            if (file->gzipped) {
                response->addHeader("Content-Encoding", "gzip");
            }
            request->send(response);
        });
    }
};

#endif

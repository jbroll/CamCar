#ifndef WEBHANDLER_H
#define WEBHANDLER_H

#include <ESPAsyncWebServer.h>
#include "FileSystem.h"

class WebHandler {
public:
    static void begin(AsyncWebServer& server) {
        // Add catch-all handler that checks filesystem
        server.onNotFound([](AsyncWebServerRequest *request) {
            // Get the requested path
            String url = request->url();
            
            // If root is requested, use "/" as the path
            const char* path = url.length() > 0 ? url.c_str() : "/";
            
            // Look up the file in our filesystem
            const FileEntry* file = FileSystem::findFileEntry(path);
            
            if (!file) {
                request->send(404, "text/plain", "File not found");
                return;
            }

            // Check for registered processor
            ContentProcessor processor = FileSystem::getProcessor(path);
            
            // Create response with proper content type
            AsyncWebServerResponse *response;
            
            if (processor) {
                // If there's a processor, use template processing
                response = request->beginResponse(200, file->content_type, 
                    file->data, file->size, processor);
            } else {
                // Otherwise serve raw file
                response = request->beginResponse(200, file->content_type,
                    file->data, file->size);
            }
                
            // Add gzip header if content is compressed
            if (file->gzipped) {
                response->addHeader("Content-Encoding", "gzip");
            }
            
            request->send(response);
        });
    }
};

#endif

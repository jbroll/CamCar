#include <pgmspace.h>
#include "../FileSystem.h"
#include "Camera_js_file.h"

extern "C" {
    extern const FileEntry Camera_js_file PROGMEM;
}

const FileEntry Camera_js_file PROGMEM = {
    .path = "/Camera.js",
    .content_type = "application/javascript",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

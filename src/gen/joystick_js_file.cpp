#include <pgmspace.h>
#include "../FileSystem.h"
#include "joystick_js_file.h"

extern "C" {
    extern const FileEntry joystick_js_file PROGMEM;
}

const FileEntry joystick_js_file PROGMEM = {
    .path = "/joystick.js",
    .content_type = "application/javascript",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

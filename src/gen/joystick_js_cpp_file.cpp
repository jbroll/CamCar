#include <pgmspace.h>
#include "../FileSystem.h"
#include "joystick_js_cpp_file.h"

extern "C" {
    extern const FileEntry joystick_js_cpp_file PROGMEM;
}

const FileEntry joystick_js_cpp_file PROGMEM = {
    .path = "/joystick_js.cpp",
    .content_type = "application/octet-stream",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

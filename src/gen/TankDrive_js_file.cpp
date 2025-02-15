#include <pgmspace.h>
#include "../FileSystem.h"
#include "TankDrive_js_file.h"

extern "C" {
    extern const FileEntry TankDrive_js_file PROGMEM;
}

const FileEntry TankDrive_js_file PROGMEM = {
    .path = "/TankDrive.js",
    .content_type = "application/javascript",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

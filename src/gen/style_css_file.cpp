#include <pgmspace.h>
#include "../FileSystem.h"
#include "style_css_file.h"

extern "C" {
    extern const FileEntry style_css_file PROGMEM;
}

const FileEntry style_css_file PROGMEM = {
    .path = "/style.css",
    .content_type = "text/css",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

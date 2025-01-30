#include <pgmspace.h>
#include "../FileSystem.h"
#include "config_html_file.h"

extern "C" {
    extern const FileEntry config_html_file PROGMEM;
}

const FileEntry config_html_file PROGMEM = {
    .path = "/config.html",
    .content_type = "text/html",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

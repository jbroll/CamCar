#include <pgmspace.h>
#include "../FileSystem.h"
#include "index_html_file.h"

extern "C" {
    extern const FileEntry index_html_file PROGMEM;
}

const FileEntry index_html_file PROGMEM = {
    .path = "/",
    .content_type = "text/html",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

#include <pgmspace.h>
#include "../FileSystem.h"
#include "error_log_file.h"

extern "C" {
    extern const FileEntry error_log_file PROGMEM;
}

const FileEntry error_log_file PROGMEM = {
    .path = "/error.log",
    .content_type = "application/octet-stream",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};

#include "../FileSystem.h"

// External references to File structures defined in individual .cpp files
extern const FileEntry config_html_file;
extern const FileEntry index_html_file;
extern const FileEntry joystick_js_file;

// Array of pointers to all files
const FileEntry* const FileSystem::files[] = {
    &config_html_file,
    &index_html_file,
    &joystick_js_file,
    nullptr  // Sentinel
};

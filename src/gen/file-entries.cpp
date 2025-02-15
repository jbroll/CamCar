#include "../FileSystem.h"

// External references to File structures defined in individual .cpp files
extern const FileEntry Camera_js_file;
extern const FileEntry TankDrive_js_file;
extern const FileEntry config_html_file;
extern const FileEntry error_log_file;
extern const FileEntry index_html_file;
extern const FileEntry joystick_js_file;
extern const FileEntry style_css_file;

// Array of pointers to all files
const FileEntry* const FileSystem::files[] = {
    &Camera_js_file,
    &TankDrive_js_file,
    &config_html_file,
    &error_log_file,
    &index_html_file,
    &joystick_js_file,
    &style_css_file,
    nullptr  // Sentinel
};

#include "../FileSystem.h"

// External references to File structures defined in individual .cpp files
extern const FileEntry index_html_file;

// Array of pointers to all files
const FileEntry* const FileSystem::files[] = {
    &index_html_file,
    nullptr  // Sentinel
};

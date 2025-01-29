#ifndef FILESYSTEM_H
#define FILESYSTEM_H
#include <cstddef>
#include <cstring>

#include <pgmspace.h>

struct FileEntry {
    const char* path;
    const char* content_type;
    const bool gzipped;
    const size_t size;
    const uint8_t* data; 
};

class FileSystem {
public:
	static const FileEntry* findFileEntry(const char* path) {
			for(const FileEntry* const* file = files; *file != nullptr; ++file) {
				if(strcmp_P(path, (*file)->path) == 0) {
					return *file;
				}
			}
			return nullptr;
	}

    static const FileEntry* const files[];
};

#endif

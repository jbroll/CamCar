#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <cstddef>
#include <cstring>
#include <pgmspace.h>
#include <map>
#include <string>

// Forward declarations
struct FileEntry;
typedef String (*ContentProcessor)(const String&);

class FileSystem {
public:
    static const FileEntry* findFileEntry(const char* path);
    static void registerProcessor(const char* path, ContentProcessor processor);
    static ContentProcessor getProcessor(const char* path);
    
    static const FileEntry* const files[];

private:
    static std::map<std::string, ContentProcessor> processors;
};

// File entry structure definition
struct FileEntry {
    const char* path;
    const char* content_type;
    const bool gzipped;
    const size_t size;
    const uint8_t* data;
};

// Implementation of inline methods
inline const FileEntry* FileSystem::findFileEntry(const char* path) {
    for(const FileEntry* const* file = files; *file != nullptr; ++file) {
        if(strcmp_P(path, (*file)->path) == 0) {
            return *file;
        }
    }
    return nullptr;
}

inline void FileSystem::registerProcessor(const char* path, ContentProcessor processor) {
    if (processor) {
        processors[std::string(path)] = processor;
    } else {
        processors.erase(std::string(path));
    }
}

inline ContentProcessor FileSystem::getProcessor(const char* path) {
    auto it = processors.find(std::string(path));
    return (it != processors.end()) ? it->second : nullptr;
}

#endif
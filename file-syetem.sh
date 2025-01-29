
output=src/gen/file-entries.cpp

cat > $output << EOF
#include "FileSystem.h"

// External references to File structures defined in individual .cpp files
$(grep PROGMEM src/gen/*.cpp | awk '{ print "extern const File "$3 ";" }')

// Array of pointers to all files
const File* const FileSystem::files[] = {
$(grep PROGMEM src/gen/*.cpp | awk '{ print "    &" $3 "," }')
    nullptr  // Sentinel
};
EOF

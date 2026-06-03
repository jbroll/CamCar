
output=src/gen/file-entries.cpp

cat > $output << EOF
#include "../FileSystem.h"

// External references to File structures defined in individual .cpp files
$(awk '/extern .* PROGMEM/ { print "extern const FileEntry "$4 ";" }' src/gen/*.cpp)

// Array of pointers to all files
const FileEntry* const FileSystem::files[] = {
$(awk '/extern .* PROGMEM/ { print "    &" $4 "," }' src/gen/*.cpp)
    nullptr  // Sentinel
};
EOF

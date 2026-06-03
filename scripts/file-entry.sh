#!/bin/bash
set -e

# Usage: ./file-entry.sh input_file
# Example: ./file-entry.sh webroot/index.html

input="$1"
filename=$(basename "$input")
filename_cpp="${filename//./_}_file.cpp"
filename_h="${filename//./_}_file.h"

varname=${filename//[.-]/_}_file

output_cpp="src/gen/$filename_cpp"
output_h="src/gen/$filename_h"

# Derive URL (use "/" for index.html, actual filename for others)
#
url=$([ "$filename" = "index.html" ] && echo "/" || echo "/$filename")

# Derive MIME type
case "$filename" in
    *.html) mime="text/html";;
    *.js) mime="application/javascript";;
    *.css) mime="text/css";;
    *.png) mime="image/png";;
    *.jpg|*.jpeg) mime="image/jpeg";;
    *.svg) mime="image/svg+xml";;
    *) mime="application/octet-stream";;
esac

function gzipper {
    gzip | od -An -v -tx1 | awk '
    BEGIN { printf "const uint8_t data_array[] = {" }
    {
        printf COMMA "\n    "
        for(i=1; i<=NF; i++) {
            printf "0x%s%s", $i, (i==NF ? "" : ", ")
        }
        COMMA = ","
    }
    END {
        if (NR*NF > 0) printf "\n"
        print "};"
    }'
}


if [ ! -f "$input" ] ; then
    echo "No input : '$input'" 1>&2
    exit 1
fi

cat $input | gzipper > $output_h

cat > "$output_cpp" << EOF
#include <pgmspace.h>
#include "../FileSystem.h"
#include "$filename_h"

extern "C" {
    extern const FileEntry $varname PROGMEM;
}

const FileEntry $varname PROGMEM = {
    .path = "$url",
    .content_type = "$mime",
    .gzipped = true,
    .size = sizeof(data_array),
    .data = data_array
};
EOF

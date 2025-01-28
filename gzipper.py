#!/usr/bin/env python3
import gzip
import sys
from pathlib import Path

def generate_header(html_file, output_file):
    # Read and compress the HTML
    with open(html_file, 'rb') as f:
        content = f.read()
    
    compressed = gzip.compress(content)
    
    # Convert to C array format
    array_data = [f"0x{b:02x}" for b in compressed]
    chunks = [', '.join(array_data[i:i+12]) for i in range(0, len(array_data), 12)]
    array_str = ',\n    '.join(chunks)
    
    # Generate the header file
    header_content = f"""// Auto-generated file - do not edit
#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <pgmspace.h>

// Gzipped HTML data
const uint8_t webpage_html_gz[] PROGMEM = {{
    {array_str}
}};

const unsigned int webpage_html_gz_len = {len(compressed)};

#endif // WEBPAGE_H
"""
    
    # Write the header file
    with open(output_file, 'w') as f:
        f.write(header_content)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} input.html output.h")
        sys.exit(1)
    
    generate_header(sys.argv[1], sys.argv[2])
    print(f"Generated header file: {sys.argv[2]}")
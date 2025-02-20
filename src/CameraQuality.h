#ifndef CAMERA_QUALITY_H
#define CAMERA_QUALITY_H

#include "esp_camera.h"
#include "Camera.h"

// Quality level definitions for camera operation
// Each level makes small adjustments to either frame size, JPEG quality, or FPS
// minKBps requirements include 50% overhead for stability before upgrade
const StreamParameters CameraHandler::QUALITY_LEVELS[] = {
    // CIF (352x288) Levels - Minimal usage
    //frameSize           jpegQual  fps  KB/frame  minKBps  fbCount
    {FRAMESIZE_CIF,           63,    5,     3,      19,      1},  // Level 0  - Initial CIF, maximum compression
    {FRAMESIZE_CIF,           50,    5,     4,      25,      1},  // Level 1  - CIF slightly better
    {FRAMESIZE_CIF,           40,    8,     6,      60,      1},  // Level 2  - CIF faster, transition ready

    // VGA (640x480) Levels - Main operating modes
    {FRAMESIZE_VGA,           63,    5,    10,      63,      1},  // Level 3  - Initial VGA, maximum compression
    {FRAMESIZE_VGA,           50,    5,    14,      88,      1},  // Level 4  - VGA better quality
    {FRAMESIZE_VGA,           55,    8,    12,     120,      1},  // Level 5  - VGA faster
    {FRAMESIZE_VGA,           40,    8,    16,     160,      1},  // Level 6  - VGA faster, better
    {FRAMESIZE_VGA,           30,    8,    20,     200,      1},  // Level 7  - VGA good quality
    {FRAMESIZE_VGA,           35,   10,    18,     225,      1},  // Level 8  - VGA 10fps
    {FRAMESIZE_VGA,           25,   10,    22,     275,      1},  // Level 9  - VGA 10fps, better
    {FRAMESIZE_VGA,           15,   10,    28,     350,      1},  // Level 10 - VGA 10fps, high quality
    {FRAMESIZE_VGA,           20,   15,    25,     469,      1},  // Level 11 - VGA 15fps
    {FRAMESIZE_VGA,           10,   15,    32,     600,      1},  // Level 12 - VGA 15fps, better
    {FRAMESIZE_VGA,            5,   15,    40,     750,      1}   // Level 13 - VGA maximum quality
};
// Define the constant array size here
const uint8_t QUALITY_LEVELS_COUNT = sizeof(CameraHandler::QUALITY_LEVELS) / sizeof(CameraHandler::QUALITY_LEVELS[0]);

#endif // CAMERA_QUALITY_H

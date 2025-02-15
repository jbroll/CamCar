#ifndef CAMERA_QUALITY_H
#define CAMERA_QUALITY_H

#include "esp_camera.h"
#include "Camera.h"

// Quality level definitions for camera operation
// Each level makes small adjustments to either frame size, JPEG quality, or FPS
// minKBps requirements include 50% overhead for stability before upgrade
const StreamParameters CameraHandler::QUALITY_LEVELS[] = {
    // QVGA (320x240) Levels - Basic connectivity
    //frameSize           jpegQual  fps  KB/frame  minKBps  fbCount
    {FRAMESIZE_QVGA,          25,    5,     4,      30,      1},  // Level 0 - Initial QVGA
    {FRAMESIZE_QVGA,          25,    8,     4,      48,      1},  // Level 1 - QVGA faster
    {FRAMESIZE_QVGA,          20,   10,     3,      45,      1},  // Level 2 - QVGA fastest

    // CIF (352x288) Levels - Intermediate size
    {FRAMESIZE_CIF,           25,    5,     5,      38,      1},  // Level 3 - Initial CIF
    {FRAMESIZE_CIF,           25,    8,     5,      60,      1},  // Level 4 - CIF faster
    {FRAMESIZE_CIF,           20,   10,     4,      60,      1},  // Level 5 - CIF fastest

    // VGA (640x480) Levels - Progressive quality improvements
    {FRAMESIZE_VGA,           30,    5,    12,      90,      1},  // Level 6  - Initial VGA
    {FRAMESIZE_VGA,           30,    8,    12,     144,      1},  // Level 7  - VGA faster
    {FRAMESIZE_VGA,           30,   10,    12,     180,      1},  // Level 8  - VGA fastest
    {FRAMESIZE_VGA,           25,   10,    10,     150,      1},  // Level 9  - VGA quality -1
    {FRAMESIZE_VGA,           20,   10,     8,     120,      1},  // Level 10 - VGA quality -2
    {FRAMESIZE_VGA,           15,   12,     6,     108,      1},  // Level 11 - VGA quality -3, higher FPS
    {FRAMESIZE_VGA,           15,   15,     6,     135,      1},  // Level 12 - VGA quality -3, fastest
    {FRAMESIZE_VGA,           12,   15,     5,     113,      1},  // Level 13 - VGA minimum quality, fastest
    {FRAMESIZE_VGA,           10,   15,     4,      90,      1},  // Level 14 - VGA lowest quality, fastest
    {FRAMESIZE_VGA,            8,   15,     3,      68,      1}   // Level 15 - VGA minimum viable, fastest
};

// Define the constant array size here
const uint8_t QUALITY_LEVELS_COUNT = sizeof(CameraHandler::QUALITY_LEVELS) / sizeof(CameraHandler::QUALITY_LEVELS[0]);

#endif // CAMERA_QUALITY_H
#ifndef CAMERA_PARAMS_H
#define CAMERA_PARAMS_H

#include "esp_camera.h"

// Define the structure first
struct StreamParameters {
    framesize_t frameSize;
    uint8_t jpegQuality;
    uint8_t targetFPS;
    size_t expectedKBPerFrame;
    float minKBps;
    uint8_t fbCount;
};

// Declare the constant array size
extern const uint8_t QUALITY_LEVELS_COUNT;

#endif // CAMERA_PARAMS_H
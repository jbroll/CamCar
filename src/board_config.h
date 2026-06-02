#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Selects the per-board pin map by compile target. Each board's map lives in a
// file named after its FQBN board id (esp32:esp32:<board>):
//   esp32:esp32:esp32s3   -> board_config_esp32s3.h   (Freenove ESP32-S3-WROOM CAM)
//   esp32:esp32:esp32cam  -> board_config_esp32cam.h  (AI-Thinker ESP32-CAM)
// The two supported boards are different chips, so we dispatch on the chip
// target macro (the esp32cam FQBN reports the generic ARDUINO_ESP32_DEV board,
// which isn't unique, but CONFIG_IDF_TARGET_* is).

#if defined(CONFIG_IDF_TARGET_ESP32S3)
  #include "board_config_esp32s3.h"
#elif defined(CONFIG_IDF_TARGET_ESP32)
  #include "board_config_esp32cam.h"
#else
  #error "CamCar: no board_config for this target (build for esp32 or esp32s3)"
#endif

#endif // BOARD_CONFIG_H

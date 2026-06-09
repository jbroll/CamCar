#ifndef BOARD_CONFIG_ESP32S3_H
#define BOARD_CONFIG_ESP32S3_H

// Pin map for the Freenove ESP32-S3-WROOM CAM (FQBN esp32:esp32:esp32s3).
// Camera pins verified against Freenove ESPHome config and Freenove GPIO notes.

// ---- Camera (OV2640, onboard -- no user wiring) ----
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  15
#define CAM_PIN_SIOD   4   // I2C SDA
#define CAM_PIN_SIOC   5   // I2C SCL
#define CAM_PIN_D7    16   // Y9
#define CAM_PIN_D6    17   // Y8
#define CAM_PIN_D5    18   // Y7
#define CAM_PIN_D4    12   // Y6
#define CAM_PIN_D3    10   // Y5
#define CAM_PIN_D2     8   // Y4
#define CAM_PIN_D1     9   // Y3
#define CAM_PIN_D0    11   // Y2
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK  13

// ---- Drive peripherals (wired by user to these free GPIOs) ----
// Pins are grouped on ONE module edge so the drive harness is a compact bundle
// and every *unused* header position can be left UNPOPULATED. Bare, unterminated
// header pins spread across the board detune/interfere with the 2.4GHz WiFi
// (observed: clipping the scattered pins restored WiFi); a small, terminated
// footprint avoids it. Two tight clusters on the same edge (module pad order
// ...14,21,47,48,45,0,35-37,38,39,40,41,42,2,1...):
//   * servo pair  -> 21 (tilt) + 47 (pan), adjacent
//   * motor block -> 39,40,41,42, then 2,1 (LED + battery) right after
// The two clusters can't merge: the octal-PSRAM pins 35-37 and strapping 45/0
// sit between 21/47 and the motor block. LIGHT stays on 14 -- it routes to the
// front with the camera, not the rear drive harness.
//   Reserved on N16R8 (do not use): 33-37 (octal PSRAM), 0/3/45/46 (strapping),
//   19/20 (USB), 43/44 (UART0), 48 (onboard NeoPixel). Battery needs ADC1 (1-10).
#define MOTOR_SPEED_PIN  -1   // SPARE (motor enable tied to 3.3V in hardware)
#define RIGHT_MOTOR_IN1  41   // PWM, sign-magnitude
#define RIGHT_MOTOR_IN2  42
#define LEFT_MOTOR_IN1   40
#define LEFT_MOTOR_IN2   39
#define PAN_PIN          47   // pan servo signal  (servo pair with TILT)
#define TILT_PIN         21   // tilt servo signal (adjacent to PAN=47)
#define LIGHT_PIN        14   // headlight LED (PWM) -- front, with the camera
#define STATUS_LED        2   // onboard LED (WiFi-connect blink)
#define BATTERY_PIN       1   // ADC1_CH0 (the old MOTOR_SPEED spare); battery divider input

#endif // BOARD_CONFIG_ESP32S3_H

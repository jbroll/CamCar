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
// Motor speed is sign-magnitude PWM on the four direction inputs (2 per motor);
// the driver's enable is tied HIGH to 3.3V in hardware, so GPIO 1 is unused/free.
#define MOTOR_SPEED_PIN   1   // SPARE (motor enable tied to 3.3V in hardware)
#define RIGHT_MOTOR_IN1  41   // PWM, sign-magnitude
#define RIGHT_MOTOR_IN2  42
#define LEFT_MOTOR_IN1   40
#define LEFT_MOTOR_IN2   39
#define PAN_PIN          47   // pan servo signal
#define TILT_PIN         21   // tilt servo signal
#define LIGHT_PIN        14   // headlight LED (PWM)
#define STATUS_LED        2   // onboard LED (WiFi-connect blink)

#endif // BOARD_CONFIG_ESP32S3_H

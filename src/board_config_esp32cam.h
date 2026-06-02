#ifndef BOARD_CONFIG_ESP32CAM_H
#define BOARD_CONFIG_ESP32CAM_H

// Pin map for the AI-Thinker ESP32-CAM (FQBN esp32:esp32:esp32cam).
//
// IO is extremely tight on this board: the camera + PSRAM (GPIO 16/17) consume
// almost everything, leaving only the SD-card pins (free since SD is unused),
// the onboard LED (33), the flash LED (4), and UART0 (1/3). The peripheral
// assignment below keeps UART0 free (you flash over it) and accepts two
// documented compromises rather than burning the UART.

// ---- Camera (OV2640, onboard -- no user wiring) ----
#define CAM_PIN_PWDN  32
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK   0
#define CAM_PIN_SIOD  26   // I2C SDA
#define CAM_PIN_SIOC  27   // I2C SCL
#define CAM_PIN_D7    35   // Y9
#define CAM_PIN_D6    34   // Y8
#define CAM_PIN_D5    39   // Y7
#define CAM_PIN_D4    36   // Y6
#define CAM_PIN_D3    21   // Y5
#define CAM_PIN_D2    19   // Y4
#define CAM_PIN_D1    18   // Y3
#define CAM_PIN_D0     5   // Y2
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22

// ---- Drive peripherals (the only GPIOs left; wire carefully) ----
// Motor driver enable tied HIGH to 3.3V in hardware (no GPIO).
#define MOTOR_SPEED_PIN  -1   // SPARE (enable tied to 3.3V)
// Motors: sign-magnitude PWM on the four ex-SD-card pins.
#define RIGHT_MOTOR_IN1  13
#define RIGHT_MOTOR_IN2  15
#define LEFT_MOTOR_IN1   14
#define LEFT_MOTOR_IN2    2
// Servos. PAN=12 is a strapping pin (MTDI, must be low at boot for 3.3V flash);
// a servo is a high-Z load so the chip's internal pulldown keeps boot safe --
// but if the board won't boot, add an external pulldown or move this signal.
// TILT=4 is also the onboard flash LED, so it will flicker while tilting.
#define PAN_PIN          12
#define TILT_PIN          4
#define LIGHT_PIN        -1   // no separate headlight (out of pins); disabled
#define STATUS_LED       33   // onboard red LED (active LOW)

#endif // BOARD_CONFIG_ESP32CAM_H

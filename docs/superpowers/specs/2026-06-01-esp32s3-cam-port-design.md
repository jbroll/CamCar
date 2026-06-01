# CamCar → Freenove ESP32-S3-WROOM CAM port

**Date:** 2026-06-01
**Status:** Approved design, pending implementation plan

## Goal

Retarget the CamCar firmware from the classic AI-Thinker ESP32-CAM to the
**Freenove ESP32-S3-WROOM CAM** board (ESP32-S3, 16 MB flash, 8 MB OPI PSRAM,
CH343 USB-serial). The classic ESP32-CAM configuration is **dropped** — this is
a single-target retarget, not a multi-board build.

Scope is the full project: live camera streaming **and** the drive/servo/light
control, with peripherals rewired to free S3 GPIOs.

## Board facts (verified)

- **Chip:** ESP32-S3 (QFN56) rev v0.2, Wi-Fi + BT5, 8 MB embedded PSRAM, 16 MB flash.
- **USB-serial:** WCH CH343 (`1a86:55d3`) → `/dev/ttyACM0` (CDC-ACM), DTR/RTS
  auto-reset works (esptool connects/resets without the BOOT button).
- **Camera GPIO map** (confirmed against two sources — Freenove ESPHome config
  and Freenove GPIO notes):
  - XCLK=15, SIOD/SDA=4, SIOC/SCL=5, VSYNC=6, HREF=7, PCLK=13
  - D0–D7 (Y2–Y9) = 11, 9, 8, 10, 12, 18, 17, 16
  - PWDN=-1, RESET=-1
- **Reserved/unavailable GPIO:** camera 4–13 & 15–18; OPI PSRAM 35/36/37;
  SD card (SDMMC 1-bit) 38/39/40; strapping 0/3/45/46; native USB 19/20;
  serial console 43/44.
- **Onboard:** GPIO2 = status LED; GPIO48 = NeoPixel. **GPIO14 is free**
  (not a camera pin).

## Pin assignment (wiring table)

Camera is onboard (no wiring). Drive hardware is wired to these free GPIOs:

| Signal | GPIO | Notes |
|---|---|---|
| Motor speed PWM (shared ENA+ENB) | 1 | tie both driver enable inputs here (shared-speed design) |
| Right motor IN1 | 41 | direction |
| Right motor IN2 | 42 | direction |
| Left motor IN1 | 40 | direction (ex-SD pin) |
| Left motor IN2 | 39 | direction (ex-SD pin) |
| Pan servo signal | 47 | |
| Tilt servo signal | 21 | |
| Light / headlight LED | 14 | PWM-dimmable |
| Status LED (WiFi blink) | 2 | onboard LED, no wiring |
| spare | 38, 48 | 38 = ex-SD; 48 = onboard NeoPixel |

**Decisions:**
- **Shared motor speed** (both enables on GPIO 1) preserves the original
  single-`Speed` control model — both motors run at the same duty.
- **SD card is sacrificed** (39/40 used for left-motor direction; SDMMC needs
  38/39/40 together and CamCar has no SD use).
- No strapping/USB/serial-console pins are used.

## Architecture: single board-config header

A new `src/board_config.h` becomes the single source of truth for **all**
hardware pins — camera GPIOs and peripheral GPIOs — so the wiring table maps
1:1 to one file and firmware/hardware cannot drift.

### Files

- **`src/board_config.h`** (new) — all pin definitions:
  - Camera GPIO macros (the map above).
  - Peripheral pins: `MOTOR_SPEED_PIN`, right/left `IN1`/`IN2`, `PAN_PIN`,
    `TILT_PIN`, `LIGHT_PIN`, `STATUS_LED`.
- **`src/Camera.h`** — replace the hardcoded `constexpr` ESP32-CAM GPIO
  constants with values from `board_config.h`.
- **`CamCar.ino`** — include `board_config.h`; remove the local
  `#define PAN_PIN/TILT_PIN/LIGHT_PIN`, `STATUS_LED`, and inline pin literals;
  `motorPins` becomes `{{1,41,42},{1,40,39}}` (struct order `{pinEn, pinIN1,
  pinIN2}`, matching the wiring table: right IN1/IN2 = 41/42, left IN1/IN2 =
  40/39; sourced from the header).
- **`src/Camera.cpp`** — confirm camera init selects the PSRAM framebuffer
  (`fb_location = CAMERA_FB_IN_PSRAM`, `fb_count = 2` when `psramFound()`),
  with a graceful path if PSRAM is missing. The CIF/VGA quality-level table
  (`CameraQuality.h`) is unchanged.

### Build

- **Makefile `BOARD`** → `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M`
  (OPI PSRAM required for the camera framebuffer on N16R8).
- **Makefile `PORT`** → `/dev/ttyACM0` (was `/dev/ttyUSB0`).
- Serial stays on UART0 → CH343 (USB-CDC-on-boot off), so `make monitor`
  is unchanged.

## Out of scope

- Multi-board / classic ESP32-CAM support (explicitly dropped).
- Independent per-motor speed control (kept shared per the original design).
- Consuming the stored `resolution`/`framesize`/`quality` prefs in the camera
  (pre-existing gap, unrelated to the port).
- SD card support.

## Risks

- **LEDC channel contention** — the camera XCLK (LEDC ch0), motor + light PWM
  (explicit ch2/3 via `ledcAttachChannel`), and ESP32Servo each consume LEDC
  resources. The S3 has 8 LEDC channels, so it should fit, but this is the most
  likely bring-up snag. Mitigation: keep motor/light on explicit channels clear
  of the camera's ch0; verify servo + camera coexist on-device.
- **PSRAM init** — wrong FQBN PSRAM mode (must be `opi`) causes camera init
  failure or no framebuffer. Verified requirement, called out in the build.

## Verification

- **Off-board:** `make build` compiles the full firmware for the S3 FQBN.
- **On-device:** `make upload && make monitor` → device joins WiFi (or SoftAP
  fallback) → load the web UI → confirm (a) video streams, (b) motors respond
  to direction + speed, (c) pan/tilt servos move, (d) light dims. LEDC/servo
  coexistence confirmed here.
- On-device camera/motor behaviour cannot be validated in CI; it requires the
  physical board and chassis.

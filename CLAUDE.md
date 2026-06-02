# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

CamCar is firmware for a **Freenove ESP32-S3-WROOM CAM** remote-control car: it streams live JPEG video and accepts drive/camera-gimbal commands over WebSockets, serving a self-contained web UI from flash, plus a high-res still endpoint. Built as an Arduino sketch compiled with `arduino-cli`.

> The project was **ported from the classic AI-Thinker ESP32-CAM to the Freenove ESP32-S3-WROOM CAM**. Anything that still says ESP32-CAM in comments/README is stale; this file and `src/board_config.h` are authoritative.

## Build & flash

All workflow goes through the `Makefile`:

- `make build` — regenerate embedded web assets, then `arduino-cli compile`
- `make upload` — build + flash over `PORT` (`/dev/ttyACM0`)
- `make monitor` — serial monitor at 115200 (**see the serial-resets-the-board gotcha below**)
- `make install` — install esp32 core + libs (ESP32Servo, "Async TCP", "ESP Async WebServer")
- `make tester` — Python FastAPI mock (`tester.py`) to exercise the UI without hardware

**FQBN:** `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app`
- **OPI PSRAM is required** (camera framebuffers, incl. UXGA snapshots).
- `huge_app` partition (the binary exceeds the default 1.25 MB app slot).
- Requires esp32 core **3.x** (verified 3.3.5–3.3.8). Uses the **pin-based LEDC API** (`ledcAttach`/`ledcAttachChannel`/`ledcWrite(pin,…)`); the old `ledcSetup`/`ledcAttachPin` were removed in core 3.x and won't compile.

`make build` (`arduino-cli compile`) is the primary way to verify **without a board** — only `upload`/`monitor` need hardware. No unit-test suite. `esp32-em.sh` (QEMU) does not work.

**WiFi credentials** are not in source: copy `.env.example` → `.env` (gitignored), set `WIFI_SSID`/`WIFI_PASSWORD`; `make build` bakes them into `src/gen/secrets.h`. No `.env` → boots a setup SoftAP. (See *WiFi provisioning*.)

## Hardware & pin map (`src/board_config.h`)

`board_config.h` is the **single source of truth** for every GPIO (camera + drive). The camera is onboard (no wiring). Drive hardware wires to:

| Signal | GPIO | Notes |
|---|---|---|
| Right motor IN1 / IN2 | 41 / 42 | PWM (sign-magnitude) |
| Left motor IN1 / IN2 | 40 / 39 | PWM |
| Motor driver **EN** | — | **tie HIGH to 3.3 V in hardware** (L298N: leave the ENA/ENB jumpers on). GPIO 1 is then **free**. |
| Pan / Tilt servo | 47 / 21 | |
| Status LED | 2 | onboard LED (WiFi-connect blink) |

Camera GPIOs (Freenove S3, verified): XCLK=15, SIOD=4, SIOC=5, VSYNC=6, HREF=7, PCLK=13, D0–D7=11/9/8/10/12/18/17/16, PWDN/RESET=-1. **microSD is sacrificed** (its pins 39/40 are reused for the left motor). NeoPixel (48) is free.

**LEDC channel budget (8 total on S3):** camera XCLK uses channel 0; motors use explicit channels **2–5** (`ledcAttachChannel`, deliberately avoiding ch 0); ESP32Servo auto-allocates the rest. Keep motor channels explicit — auto-allocation can collide with the camera's ch 0.

## ⚠️ The XCLK lesson (most important thing in this repo)

The camera XCLK **boots at 8 MHz** (`XCLK_FREQ_HZ` in `Camera.cpp`) — the always-safe default. At many frequencies the LEDC-generated clock **radiates into the 2.4 GHz WiFi band** and destroys throughput (erratic 0–2 fps, multi-hundred-ms stalls, TCP retransmits) — see espressif/arduino-esp32 #5834. This was the root cause of severe streaming lag; it is an **RF/hardware** issue, not CPU/task contention. Tradeoff at 8 MHz: the sensor is clock-limited to ~10 fps, but resolution is nearly free up to XGA.

**Runtime XCLK + sweep findings.** `CameraHandler::setXclkFreq()` re-inits the sensor at a new XCLK at runtime (WiFi untouched); the `Xclk,<MHz>` command (fractional OK) and the UI `xclkSelect` dropdown drive it. **Boot is always 8 MHz**; the menu choice is session-only (a reboot returns to safe 8). A measured sweep (unpaced `/stream` at SVGA, scoring fps + inter-frame jitter + stalls) showed the interference is **not monotonic** — it's a comb of narrow *clean bands* separated by *dirty notches*, because the bad spots are where an XCLK harmonic lands in the AP's WiFi channel. So clean frequencies are **per-board and per-channel**: on this network the **Freenove S3 is clean at 13.5–14.5 MHz (~18 fps, +80%)**, while the **AI-Thinker is clean at ~17 MHz (~11 fps)** and marginal elsewhere. Those positions shift if the WiFi channel changes — hence 8 MHz stays the documented fallback and the boot default.

## Camera streaming (`src/Camera.h` / `Camera.cpp`)

`CameraHandler` owns capture + streaming. Init is at **UXGA** so the framebuffer fits any resolution; the sensor then runs at the streaming size. PSRAM framebuffer, `fb_count=2`, `GRAB_WHEN_EMPTY`.

- **`sendFrame()`** (called from `loop()`): paces to `mTargetFPS`, captures, sends the whole JPEG as one `wsCamera.binary()` message, then **returns the fb immediately** (binary() copies the data — do *not* hold the camera buffer; holding it serialized the pipeline and caused multi-second stalls). Under backpressure (`queueLen() > MAX_INFLIGHT_FRAMES`) it **drops** a frame to bound latency.
- **Resolution control** is by **ladder index**, NOT raw `framesize_t` value (see enum gotcha). `RES_LADDER` = QVGA/CIF/VGA/SVGA/XGA; `setResolution(index)` sets the ceiling and jumps to it.
- **Auto-adapt** (`adaptAndReport`, every 2 s): if >25 % of frame-slots were dropped, step the resolution **down** one rung; after 2 clean windows step **up** toward the user-selected ceiling. Reuses the frame-drop as the congestion signal; dormant on a clear link. Replaced an over-built 14-level quality controller that was removed.
- **Stream report:** every 2 s `sendFrame` prints `[stream] <fps> | <mode> | dropped <n> | queue <n> | RSSI <dBm>` and `[adapt]` on each step.

**Snapshot:** `GET /snapshot?res=<0–4>[&download=1]` (handler in `CamCar.ino`) → `CameraHandler::captureSnapshot(index)`. It cooperatively **pauses** `sendFrame` (a flag the loop acknowledges), switches the sensor up to `SNAP_LADDER` size (VGA…**UXGA 1600×1200**) at higher quality, grabs one frame, restores, resumes. The fb is streamed via a filler response and freed after the last chunk. UI has a snapshot picker + View/Save buttons.

## Motor & servo control (`CamCar.ino`)

**Proportional differential (tank) drive via sign-magnitude PWM.** No separate enable channels: enable is tied HIGH in hardware, and each motor's speed is PWM on whichever **input** matches its direction (`driveMotor(in1,in2,speed)`; PWM the active input, hold the other low). This gives independent per-track proportional speed.

- `tankDrive(x,y)` arcade-mixes throttle (y) and turn (x) → left/right speeds.
- `cameraControl(x,y)` → pan/tilt servo angles.

**Control protocol** (`/CarInput` WebSocket text frames):
- `tank <x> <y>` and `camr <x> <y>` — space-delimited, x/y in −100..100 (sent by the UI joysticks).
- Comma-delimited `key,value`: `Resolution,<index>` (ladder index), `Quality,<q>` (JPEG q 4–63, **lower = sharper/bigger**), `Lock,<0|1>` (1 = freeze resolution / disable auto-adapt), `Light,<0|1>` (headlight on `LIGHT_PIN`).
- The parser reads the first whitespace token; if it's `tank`/`camr` it dispatches those, else it falls back to comma `key,value`. Disconnect → safe-stop + center servos.

**Status frames (device → page):** the camera socket also sends text frames the page distinguishes from binary JPEG by type: `up <seconds>` (device uptime, from `adaptAndReport`) and — where `BATTERY_PIN >= 0` (S3 GPIO 1 = ADC1_CH0, the old motor-EN spare; no free ADC1 pin on the ESP32-CAM) — `bat <volts> <percent>` every 2 s from `loop()`. Battery wiring is a resistor divider into `BATTERY_PIN`; `BATTERY_DIVIDER`/`BATTERY_VMIN`/`BATTERY_VMAX` in `CamCar.ino` calibrate it (defaults: 200k/100k divider, 2S LiPo 6.0–8.4 V). Read via `analogReadMilliVolts` (factory-calibrated), averaged over 8 samples.

**HTTP-MJPEG (`GET /stream`):** a `multipart/x-mixed-replace; boundary=frame` chunked response (filler in `src/MjpegStream.h`) for VLC/ffmpeg/NVRs. The camera has one framebuffer, so while a `/stream` client is connected it becomes the sole **grabber** (`CameraHandler::setHttpStreaming`): the MJPEG filler grabs each frame and `publishSharedFrame()`s it into a cross-core double buffer; `sendFrame()` in `loop()` then **forwards** those same frames to the WS client instead of grabbing. So a WS viewer and a `/stream` viewer run **concurrently on the same frames** (verified: WS stays ~10 fps with MJPEG connected, identical frame sizes on both). The publish is skipped (no copy) when no WS client is connected. `request->onDisconnect` flips grabbing back to the WS path. Still MJPEG (the OV2640/S3 have no H.264 encoder) and ~10 fps at the 8 MHz XCLK. Note: the *grab* happens in the async task while MJPEG streams (blocks it up to ~one frame interval) — a deliberate trade for full MJPEG fps; the alternative (`RESPONSE_TRY_AGAIN`) would be poll-limited to ~2 fps.

## Embedded web-asset pipeline

`webroot/` files are gzipped at build time into C++ under `src/gen/` and served from PROGMEM (no filesystem):
- `file-entry.sh` → `<name>_file.cpp/.h` (gzipped byte array + `FileEntry` struct).
- `file-system.sh` → `file-entries.cpp` (the `FileSystem::files[]` table).
- `gen-secrets.sh` → `secrets.h` from `.env`.

The `Makefile` `gen-sources` target runs all three before compiling. **`src/gen/` is generated and gitignored — never edit by hand; edit `webroot/`/`.env` and rebuild.** The Arduino IDE won't regenerate them — use `make`. `WebHandler::begin` serves them via an `onNotFound` lookup with `Content-Encoding: gzip`.

> The live camera renderer is the **inline** handler in `webroot/index.html` (img.src + revoked object URLs). `webroot/Camera.js` is **dead code, not loaded** — don't edit it expecting changes.

## WiFi provisioning (`setupWiFi()`)

`.env` → `secrets.h` (build-time defaults) → seeded into NVS on first boot → NVS is authoritative. STA join with a 15 s timeout; on no-creds or timeout, a setup SoftAP (`CamCar-setup` / `camcarsetup`) serves `http://192.168.4.1/config`. `WiFi.setSleep(false)` is set **after** association (it doesn't stick before). RSSI is logged at connect. Build-time defaults are recoverable from a flash dump (out of git, not secret from physical access).

## Config persistence (`src/PrefEdit.h`)

NVS key/values via `Preferences` (namespace `config`, keys in `Prefs.h`), editable at `/config`. `initStorage()` opens NVS before the server exists (used by `setupWiFi()` via `get`/`set`); changing `ssid`/`password` schedules a deferred reboot via `PrefEdit::loop()` so the HTTP response flushes first.

## Gotchas & hard-won lessons

- **Opening a serial monitor RESETS the board** (the CH343 toggles the reset line on port open) → it reboots and the video stream drops. So serial console and live video are mutually exclusive. Use the **in-browser stats overlay** (fps/resolution/KB, computed client-side) for live monitoring, not `make monitor`.
- **`framesize_t` enum values are version-dependent.** This esp32-camera build renumbered them (CIF=8, VGA=10, etc.), so the classic JS values (VGA=8…) selected the wrong size or were rejected. **Always use ladder indices** across the JS/firmware boundary, mapped through symbolic `FRAMESIZE_*` constants — never raw enum integers.
- **Diagnosing WiFi vs. firmware:** `ping` the board (bypasses camera/WS/browser). The dev laptop is likely on **5 GHz**; the ESP32-S3 is **2.4 GHz-only**, so ping the board *and* the gateway to compare. Disabling the camera and re-pinging isolates camera-clock interference from RF environment.
- **Single camera viewer:** the firmware streams to one `mClientId`; a second `/Camera` connection steals the stream. Two tabs/probes fight each other.
- **WiFi credentials were committed in plaintext historically and were scrubbed from git history** (force-pushed). Treat the old `lucky7`/`snowblower` password as compromised — rotate it. `.env` and `src/gen/secrets.h` are gitignored.
- Header-only classes define inline static members in the `.h` (`PrefEdit`) — include in exactly one TU.
- The repo root holds stale `*.ino-save` snapshots; only `CamCar.ino` + `src/` build. `README.md` is outdated — trust the `Makefile` and this file.

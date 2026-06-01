# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

CamCar is firmware for an ESP32-CAM remote-control car: it streams live JPEG video and accepts drive/servo/light commands over WebSockets, serving a self-contained web UI from flash. Built as an Arduino sketch compiled with `arduino-cli`.

## Build & Run

All workflow goes through the `Makefile`:

- `make build` — regenerate embedded web assets, then `arduino-cli compile` for FQBN `esp32:esp32:esp32cam`
- `make upload` — build + flash over `PORT` (default `/dev/ttyUSB0`)
- `make monitor` — serial monitor at 115200 baud
- `make try` — upload then monitor
- `make install` — install the esp32 core and libs (ESP32Servo, ESPAsyncWebServer, AsyncTCP) via `arduino-cli`
- `make ports` — list connected boards
- `make tester` — create a Python venv and run `tester.py`, a FastAPI mock server (port 8000) that stubs the `/CarInput` and `/Camera` WebSockets so the UI can be exercised without hardware. Note: `tester.py` opens `index.html` from the cwd, but the real file lives in `webroot/` — run from there or adjust the path.

Requires esp32 core **3.x** (`make install` pulls the latest; verified against 3.3.5–3.3.8). The firmware uses the **pin-based LEDC API** (`ledcAttach`/`ledcAttachChannel`/`ledcWrite(pin, …)`) introduced in core 3.x — the old channel-based `ledcSetup`/`ledcAttachPin` were removed and will not compile.

`make build` (i.e. `arduino-cli compile`) is the primary way to verify changes **without a board** — only `upload`/`monitor` need hardware. There is no unit-test suite. `esp32-em.sh` is an incomplete QEMU experiment and does not work.

WiFi credentials are **not** in source (see *WiFi provisioning* below). Copy `.env.example` → `.env` (gitignored), set `WIFI_SSID`/`WIFI_PASSWORD`, and `make build` bakes them into `src/gen/secrets.h`. With no `.env`, the firmware boots into a setup SoftAP instead.

## Embedded web-asset pipeline (most important architecture)

The web UI in `webroot/` (`index.html`, `*.js`, `*.css`, `config.html`) is **not** served from a filesystem. At build time each file is gzipped and emitted as a C++ source file under `src/gen/`:

- `file-entry.sh <webroot/file>` → `src/gen/<name>_file.cpp` + `.h`. It gzips the file into a `data_array[]` byte array (`.h`) and wraps it in a `FileEntry` PROGMEM struct (path, MIME type, gzipped flag, size, data pointer). The URL is derived from the filename (`index.html` → `/`).
- `file-system.sh` scans `src/gen/*.cpp` for the `extern ... PROGMEM` declarations and generates `src/gen/file-entries.cpp`, the `FileSystem::files[]` null-terminated pointer array.
- `gen-secrets.sh` reads the untracked `.env` and emits `src/gen/secrets.h` (`WIFI_SSID_DEFAULT` / `WIFI_PASSWORD_DEFAULT`); a missing/empty `.env` yields empty defaults.

The `Makefile` runs all three automatically (`gen-sources` target) before compiling, keyed off `webroot/` and `.env` mtimes. **`src/gen/` is entirely generated — never edit it by hand; edit `webroot/` (or `.env`) and rebuild.** `src/gen/` is gitignored (the generated files used to be committed; they were untracked deliberately, so a clone builds them via `make`). Building in the **Arduino IDE** would not regenerate them — use `make`/`arduino-cli`.

At runtime, `WebHandler::begin` (`src/WebHandler.h`) installs an `onNotFound` catch-all that looks up the request URL in `FileSystem::findFileEntry`, optionally runs a registered `ContentProcessor` template processor, and serves the PROGMEM bytes with a `Content-Encoding: gzip` header. This is how all static content reaches the browser.

## Runtime structure (`CamCar.ino`)

- **Networking:** an `AsyncWebServer` on port 80 plus two `AsyncWebSocket`s — `/Camera` (video out) and `/CarInput` (commands in). `setupWiFi()` reads credentials from NVS and joins as a station, falling back to a setup SoftAP (see *WiFi provisioning*).
- **Control protocol:** `/CarInput` receives comma-separated `key,value` text frames. Keys: `MoveCar` (direction enum UP/DOWN/LEFT/RIGHT/STOP), `Speed`, `Light` (both PWM duty), `Pan`, `Tilt` (servo angles). On disconnect the car is stopped and servos re-centered.
- **Motors:** `CamCar.ino` drives the two motors directly via `motorPins` GPIO + a shared PWM speed channel on explicit LEDC channel 2 (`rotateMotor`/`moveCar`); both motor-enable pins are wired to the same GPIO, so the speed PWM is attached once. The camera driver uses LEDC channel 0, so motor/light PWM use explicit channels 2/3 (`ledcAttachChannel`) to avoid collision. The `DifferentialDrive`/`DCMotor` classes (`src/TankDrive.h`, `src/DCMotor.h`) are a cleaner alternative abstraction that **exists but is currently unused** by the sketch.
- **Main loop:** just cleans up WS clients and calls `camera.sendFrame()`.

## Adaptive camera streaming (`src/Camera*`)

`CameraHandler` (`src/Camera.h` / `Camera.cpp`) owns capture and streaming with automatic quality adaptation:

- `QUALITY_LEVELS[]` (`src/CameraQuality.h`) is an ordered table of 14 `StreamParameters` (`src/CameraParams.h`) — framesize / JPEG quality / FPS / bandwidth tiers from CIF up to VGA. `mCurrentQualityLevel` indexes it.
- `sendFrame()` paces to target FPS, captures a JPEG, and pushes it over the `/Camera` WebSocket in `WS_BUFFER_SIZE` chunks; one frame transmits at a time (`mTransmissionInProgress`).
- `checkCongestion()` watches the WebSocket client queue and steps the quality level down on backpressure / up after a stability period (`QUALITY_STABILITY_PERIOD_MICROS`, `UPGRADE_STABILITY_PERIOD_MICROS`). Capture failures are tracked and retried.

## Config persistence (`src/PrefEdit.h`)

`PrefEdit` stores key/values in ESP32 NVS (`Preferences`, namespace `"config"`; keys listed in `Prefs.h`). `begin(server, "/config", configParams)` registers a `ContentProcessor` so `config.html` template variables are filled with stored values, and handles `HTTP_POST /config` to update them (same `FileSystem` processor hook as above). API split so boot code can use it before the server exists:

- `initStorage()` — opens NVS; idempotent (guarded by `_inited`), callable before `begin()`.
- `get(key, def)` / `set(key, value)` — public read/write, used by `setupWiFi()` to read and seed credentials.
- `handleUpdate()` — on a `ssid`/`password` change, schedules a deferred reboot (`_rebootAt`); `PrefEdit::loop()` (called from the main `loop()`) performs `ESP.restart()` ~1s later so the HTTP response flushes first. New WiFi credentials only take effect on reconnect, hence the reboot.

Note: `resolution`/`framesize`/`quality` are collected by `/config` and stored, but **not yet consumed** by `Camera.cpp` — a known stored-but-unused gap.

## WiFi provisioning (`CamCar.ino` `setupWiFi()`)

Credentials live in NVS, never in source. Boot sequence:

1. `.env` (untracked) → `src/gen/secrets.h` at build time (`WIFI_SSID_DEFAULT`/`WIFI_PASSWORD_DEFAULT`).
2. On **first boot**, if NVS has no `ssid` and a build-time default exists, it's seeded into NVS. NVS is the source of truth thereafter (survives reflashes; `/config` edits stick).
3. Join as a station with a **15s timeout** (`WIFI_CONNECT_TIMEOUT_MS`) — no more infinite connect loop.
4. If no stored SSID **or** the join times out, start a **setup SoftAP** (`CamCar-setup` / `camcarsetup`) so `http://192.168.4.1/config` is reachable to enter credentials, which then reboots into station mode.

So a fresh device with no `.env` is self-provisioning over SoftAP; a developer sets `.env` for convenience. Caveat: build-time defaults are compiled into the firmware image and recoverable from a flash dump — out of git, not secret from physical access.

## Conventions & gotchas

- Header-only classes use inline static-member definitions in the `.h` (e.g. `PrefEdit::_prefs`, `_rebootAt`) — include such a header in exactly one translation unit to avoid duplicate-symbol link errors.
- The repo root holds stale snapshots (`*.ino-save`, `*.ino.save`, `SAVE.ino.save`, `Test-ino.save`) — these are not built; only `CamCar.ino` and `src/` compile.
- `README.md` references some filenames that have since changed (`test_serv.py` → `tester.py`, `gzipper.py` → logic now inside `file-entry.sh`). Trust the `Makefile` and this file over the README for the current build flow.

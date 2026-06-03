# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

CamCar is firmware for a **Freenove ESP32-S3-WROOM CAM** remote-control car: it streams live JPEG video and accepts drive/camera-gimbal commands over WebSockets, serving a self-contained web UI from flash, plus a high-res still endpoint. Built as an Arduino sketch compiled with `arduino-cli`.

> The project was **ported from the classic AI-Thinker ESP32-CAM to the Freenove ESP32-S3-WROOM CAM**. Anything that still says ESP32-CAM in comments/README is stale; this file and `src/board_config.h` are authoritative.

## Build & flash

All workflow goes through the `Makefile`:

- `make build` — regenerate embedded web assets, then `arduino-cli compile`
- `make upload` — build + flash over `PORT` (`/dev/ttyACM0`)
- `make monitor` — serial monitor at 115200 (**see the serial-resets-the-board gotcha below**)
- `make install` — install esp32 core + libs (ESP32Servo, "Async TCP", "ESP Async WebServer"). Micro-RTSP is **vendored in-repo** (`libraries/Micro-RTSP`, passed via `--libraries`), so it needs no install.
- `make test` — functional suite (`tests/functional.py`) against a live board: every transport + camera-config cycling. Override the target with `HOST=` (e.g. `make test HOST=camcar-840d8e.local`). Stdlib-only (plus ffmpeg for RTSP) — no venv.

**FQBN:** `esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app`
- **OPI PSRAM is required** (camera framebuffers, incl. UXGA snapshots).
- `huge_app` partition (the binary exceeds the default 1.25 MB app slot).
- Requires esp32 core **3.x** (verified 3.3.5–3.3.8). Uses the **pin-based LEDC API** (`ledcAttach`/`ledcAttachChannel`/`ledcWrite(pin,…)`); the old `ledcSetup`/`ledcAttachPin` were removed in core 3.x and won't compile.

`make build` (`arduino-cli compile`) is the primary way to verify **without a board** — only `upload`/`monitor`/`test` need hardware. `make test` is a live-board functional suite (no on-host unit tests).

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

`CameraHandler` owns capture + streaming. Init is at **UXGA** so the framebuffer fits any resolution; the sensor then runs at the streaming size. PSRAM framebuffer, `fb_count=2`, `GRAB_LATEST` (freshest frame, lowest latency).

- **`sendFrame()`** is the **single producer** (called from `loop()`): it is the *only* code that grabs the camera. One fps-cap pacing gate (`mTargetFPS`) — this is the single rate limit that bounds the link for **all** viewers. It captures, `publishSharedFrame()`s the JPEG into a cross-core double buffer (for the stream-server consumers), then sends to every WS `/Camera` client via `wsCamera.getClients()` + `binary(shared)` with per-client `queueLen() <= MAX_INFLIGHT_FRAMES` backpressure (one shared buffer → copied once for all), and **returns the fb immediately**. It only grabs when someone is watching: `wsCamera.count() > 0 || streamClients() > 0`.
- **Consumers never grab.** The MJPEG/RTSP stream servers read the producer's frames via `CameraHandler::copyLatestFrame(dst, cap, &len, lastSeq)` (returns a new seq if a frame newer than `lastSeq` exists). `addStreamClient()`/`removeStreamClient()` keep the producer running while any stream client is connected even with no WS viewer. See **Streaming servers** below.
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

## Streaming servers (`src/StreamServer.h`, `MjpegStreamServer.h`, `RtspStreamServer.h`)

The WS `/Camera` live view runs on **AsyncWebServer** (event loop on the `async_tcp` task). The byte-pump streams want to **block** (wait for the next paced frame, then blocking-write), which is forbidden on `async_tcp` — *all* AsyncTCP handlers/sockets share **one** global `async_tcp` task (verified in `AsyncTCP.cpp`: single `_async_service_task_handle`), so blocking any handler starves the WS control channel. So the streams live elsewhere.

**`StreamServer`** (base) `extends WiFiServer`: own listening port + own **dedicated FreeRTOS task** running `run()`. On its own task blocking writes are fine — they park only that task, never `async_tcp` (control) or `loop()` (the producer). Subclasses implement `run()` and read frames via `copyLatestFrame()`; they never grab the camera. This is Espressif's CameraWebServer split (control on 80, stream on 81) and the esp32cam-rtsp `WiFiServer`-extends pattern, but keeping AsyncWebServer for UI/WS.

- **`MjpegStreamServer`** — `http://<host>:81/stream`, `multipart/x-mixed-replace`, for VLC/ffmpeg/NVRs/Motion. **Multi-client, non-blocking multiplex** (up to `MAX_CLIENTS`, default 4): keeps a `std::list<Client>`, refreshes one PSRAM staging copy of the latest frame via `copyLatestFrame()`, then fans it into each client's own PSRAM buffer. Each loop pushes per client with `::send(fd, …, MSG_DONTWAIT)`, advancing a per-client write offset — partial writes resume next iteration, so a slow client only drops frames (skips to the newest staged frame when it finishes) and never stalls the others. **Do not gate writes on `availableForWrite()`** — it returns 0 on this lwip client (this was the original single-client bug: frames loaded but the write offset never advanced); `MSG_DONTWAIT` send is the reliable non-blocking write and also surfaces `ECONNRESET`/`EPIPE` for prompt client reaping. HTTP headers go out on a normal blocking `print()` (tiny, fresh socket). Verified: 3 curl clients + a Motion server concurrently, each at full producer fps (~17–18 at 14 MHz XCLK).
- **`RtspStreamServer`** — `rtsp://<host>:554/mjpeg/1`, RTP/JPEG (RFC 2435) via **vendored Micro-RTSP** (`libraries/Micro-RTSP`, a minimal `CStreamer`+`CRtspSession` subset). `SharedFrameStreamer` feeds `copyLatestFrame()` instead of grabbing; we patched `CStreamer::setDimensions()` so the RTP width/height tracks the runtime resolution (auto-adapt) per frame. **Session ownership (patched):** `RtspStreamServer::run()`'s reap loop is the *sole* owner of `CRtspSession` deletion — it deletes the session, frees the `WiFiClient*`, and decrements the stream-client count. We removed the `delete session` from `CStreamer::handleRequests` (stock Micro-RTSP deleted there too, which double-freed / use-after-free read `m_stopped` on the ordinary TEARDOWN/disconnect path and rebooted the board). **Multiple concurrent sessions** are supported: the per-request gluing buffer/cursor/state in `CRtspSession::handleRequests` were function statics shared across all sessions (cross-session parse corruption) — now per-session members (`m_RecvBuf`/`m_RecvBufPos`/`m_HdrState`). Note the static RTP packet buffer (`CStreamer::SendRtpPacket`'s `RtpBuf[2048]`) is still shared, which is safe because all sessions are serviced sequentially on the one `rtsp_stream` task.

All MJPEG (the OV2640 has no H.264 encoder). Verified on both boards: the **single producer** feeds WS + `:81` MJPEG + `:554` RTSP **concurrently** (S3 ~16/16/13 fps at 14 MHz XCLK), and `/CarInput` WS ping→pong RTT stays flat under stream load (median ~5 ms, unchanged) — proving the pumps never block `async_tcp`. (`make build` requires `--libraries libraries` for the vendored lib; the Makefile sets it.)

## Embedded web-asset pipeline

`webroot/` files are gzipped at build time into C++ under `src/gen/` and served from PROGMEM (no filesystem):
- `scripts/file-entry.sh` → `<name>_file.cpp/.h` (gzipped byte array + `FileEntry` struct).
- `scripts/file-system.sh` → `file-entries.cpp` (the `FileSystem::files[]` table).
- `scripts/gen-secrets.sh` → `secrets.h` from `.env`.

The `Makefile` `gen-sources` target runs all three before compiling. **`src/gen/` is generated and gitignored — never edit by hand; edit `webroot/`/`.env` and rebuild.** The Arduino IDE won't regenerate them — use `make`. `WebHandler::begin` serves them via an `onNotFound` lookup with `Content-Encoding: gzip`.

> The live camera renderer is the **inline** handler in `webroot/index.html` (img.src + revoked object URLs). `webroot/Camera.js` is **dead code, not loaded** — don't edit it expecting changes.

## WiFi provisioning (`setupWiFi()`)

`.env` → `secrets.h` (build-time defaults) → seeded into NVS on first boot → NVS is authoritative. STA join with a 15 s timeout; on no-creds or timeout, a setup SoftAP (`CamCar-setup` / `camcarsetup`) serves `http://192.168.4.1/config`. `WiFi.setSleep(false)` is set **after** association (it doesn't stick before). RSSI is logged at connect. Build-time defaults are recoverable from a flash dump (out of git, not secret from physical access).

## Config persistence (`src/PrefEdit.h`)

NVS key/values via `Preferences` (namespace `config`, keys in `src/Prefs.h`), editable at `/config`. `initStorage()` opens NVS before the server exists (used by `setupWiFi()` via `get`/`set`); changing `ssid`/`password` schedules a deferred reboot via `PrefEdit::loop()` so the HTTP response flushes first.

## Gotchas & hard-won lessons

- **Opening a serial monitor RESETS the board** (the CH343 toggles the reset line on port open) → it reboots and the video stream drops. So serial console and live video are mutually exclusive. Use the **in-browser stats overlay** (fps/resolution/KB, computed client-side) for live monitoring, not `make monitor`.
- **`framesize_t` enum values are version-dependent.** This esp32-camera build renumbered them (CIF=8, VGA=10, etc.), so the classic JS values (VGA=8…) selected the wrong size or were rejected. **Always use ladder indices** across the JS/firmware boundary, mapped through symbolic `FRAMESIZE_*` constants — never raw enum integers.
- **Diagnosing WiFi vs. firmware:** `ping` the board (bypasses camera/WS/browser). The dev laptop is likely on **5 GHz**; the ESP32-S3 is **2.4 GHz-only**, so ping the board *and* the gateway to compare. Disabling the camera and re-pinging isolates camera-clock interference from RF environment.
- **Single camera viewer:** the firmware streams to one `mClientId`; a second `/Camera` connection steals the stream. Two tabs/probes fight each other.
- **WiFi credentials were committed in plaintext historically and were scrubbed from git history** (force-pushed). Treat the old `lucky7`/`snowblower` password as compromised — rotate it. `.env` and `src/gen/secrets.h` are gitignored.
- Header-only classes define inline static members in the `.h` (`PrefEdit`) — include in exactly one TU.
- The build is `CamCar.ino` (root, required by Arduino) + everything under `src/`. Build/asset-gen scripts live in `scripts/`, the UI mock + Python deps in `tools/`, the live-board suite in `tests/`.

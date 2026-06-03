# CamCar — ESP32-S3 camera car

Firmware for a **Freenove ESP32-S3-WROOM CAM** remote-control car. It streams
live JPEG video and accepts drive / camera-gimbal commands over WebSockets,
serving a self-contained web UI from flash. The same firmware also builds for
the classic **AI-Thinker ESP32-CAM** (camera-focused).

Built as an Arduino sketch compiled with `arduino-cli` via the `Makefile`.
For the authoritative, in-depth notes (architecture, gotchas, the hard-won
lessons), see [`CLAUDE.md`](CLAUDE.md) — it is kept current; this README is the
overview.

## Features

- **Live video** three ways, all fed concurrently from one paced capture
  (a single producer fans each frame out to every viewer):
  - in-page WebSocket JPEG stream (the web UI; one viewer at a time)
  - **HTTP-MJPEG**, **multi-client**, at `:81/stream`
    (`multipart/x-mixed-replace`) for VLC, ffmpeg, Home Assistant, NVRs, Motion
  - **RTSP / RTP-JPEG**, **multi-client**, at `rtsp://<host>:554/mjpeg/1`
    (TCP-interleaved or UDP) for NVR-native clients
- **Tank drive** — proportional differential drive on both tracks (sign-magnitude
  PWM), plus **pan/tilt** camera gimbal, both via on-screen multi-touch joysticks
- **High-res snapshot** — `GET /snapshot?res=<0–4>[&download=1]`, up to UXGA
  (1600×1200); view or download
- **Camera controls** in the UI — resolution and JPEG-quality menus, a
  resolution **lock**, a **headlight** toggle, and a **camera stop** button
- **Adaptive resolution** — backs off under WiFi congestion, recovers when clear
- **XCLK tuning** — a camera-clock menu and one-click **auto-tune** that finds the
  fastest WiFi-clean clock for your network (see *The XCLK story* below)
- **Status readouts** — fps / resolution / KB-per-frame, device uptime, and
  battery voltage (S3, with a divider on GPIO 1)
- **Network niceties** — `camcar-<id>.local` via mDNS, a DHCP hostname, and a
  `/config` page for WiFi + settings

## Hardware

The camera is onboard (no wiring). Drive hardware wires to the GPIOs defined in
[`src/board_config.h`](src/board_config.h) — that file (per board) is the single
source of truth for every pin. Summary for the **Freenove S3**:

| Signal | GPIO | Notes |
|---|---|---|
| Right motor IN1 / IN2 | 41 / 42 | PWM (sign-magnitude) |
| Left motor IN1 / IN2 | 40 / 39 | PWM |
| Motor driver **EN** | — | **tie HIGH to 3.3 V** (leave L298N ENA/ENB jumpers on) |
| Pan / Tilt servo | 47 / 21 | |
| Headlight LED | 14 | |
| Battery sense | 1 | ADC1_CH0, via a resistor divider (optional) |
| Status LED | 2 | onboard |

The AI-Thinker ESP32-CAM map (tighter — IO is mostly consumed by the camera) is
in [`src/board_config_esp32cam.h`](src/board_config_esp32cam.h).

## Build & flash

Everything goes through the `Makefile`. The board defaults to the S3; override
with `TARGET=cam` for the AI-Thinker.

```bash
make install              # esp32 core 3.x + libs (ESP32Servo, Async TCP, ESP Async WebServer)
make build                # regenerate embedded web assets, then arduino-cli compile
make upload               # build + flash over PORT (S3: /dev/ttyACM0, CAM: /dev/ttyUSB0)
make monitor              # serial monitor @ 115200  (resets the board — see Gotchas)
make tester               # Python mock (tester.py) to exercise the UI without hardware

make build  TARGET=cam    # ...any target for the AI-Thinker ESP32-CAM
```

`make build` (compile) is the primary way to verify **without a board**.

**WiFi credentials** are not in source: copy `.env.example` → `.env` (gitignored),
set `WIFI_SSID` / `WIFI_PASSWORD`; the build bakes them into `src/gen/secrets.h`,
which is seeded into NVS on first boot. With no credentials the board starts a
setup access point (`CamCar-setup` / `camcarsetup`) serving
`http://192.168.4.1/config`.

## Using it

1. Flash, then find the board: `http://camcar-<id>.local/` (mDNS) or the IP from
   the serial log / your router.
2. Drive with the left joystick, aim the camera with the right; the top-right
   menus set resolution / quality / camera clock; buttons handle lock, headlight,
   snapshot, and camera stop.
3. For an external viewer (VLC, ffmpeg, NVR, Motion) open the HTTP-MJPEG stream
   at `http://<host>:81/stream` or the RTSP stream at `rtsp://<host>:554/mjpeg/1`.
   Both accept several simultaneous clients.

### Endpoints

| Path | What |
|---|---|
| `/` | web UI |
| `/Camera` (WS) | live JPEG frames |
| `/CarInput` (WS) | drive / camera / settings commands |
| `:81/stream` | HTTP-MJPEG (`multipart/x-mixed-replace`), multi-client |
| `rtsp://<host>:554/mjpeg/1` | RTSP / RTP-JPEG, multi-client (TCP or UDP) |
| `/snapshot?res=<0–4>[&download=1]` | one high-res still (up to UXGA) |
| `/config` | WiFi + settings editor (NVS) |

## The XCLK story (the most important thing to know)

The camera's master clock (XCLK) **boots at 8 MHz** — the always-safe value.
Raising it increases the frame rate, but at many frequencies the LEDC-generated
clock radiates into the 2.4 GHz band and wrecks WiFi throughput. The clean
frequencies are a *comb of narrow bands* whose positions depend on your board
**and** your WiFi channel — so there's no universal "best" value.

The UI's **Auto-tune** button handles this: the firmware sweeps 8–20 MHz (0.5 MHz
steps), scores each by delivered fps, and adopts + persists the fastest clean one
(e.g. ~14.5 MHz / ~+80 % fps on a clean S3 link). It **boots safe at 8 MHz** and
applies the saved value only after WiFi associates; the **camera-stop** button is
the recovery if a saved clock ever disturbs WiFi (stopping it halts the XCLK and
clears the air). Full background is in `CLAUDE.md`.

## Gotchas

- **Opening a serial monitor resets the board** (the USB-serial chip toggles
  reset), so the video stream drops. Use the in-browser stats overlay for live
  monitoring, not `make monitor`.
- **Single WS viewer** — the in-page `/Camera` WebSocket streams to one client at
  a time; a second `/Camera` connection steals the stream. (The `:81` MJPEG and
  `:554` RTSP servers each serve several concurrent clients — only the WS path is
  single-viewer.)
- `src/gen/` is generated and gitignored — edit `webroot/` / `.env` and rebuild.

## License & credits

MIT — see [LICENSE](LICENSE).

- Original ESP32-CAM project by **Ujwal Nandanwar**
- Ported to the Freenove ESP32-S3 and extended by **John B. Roll Jr.**
- Joystick UI adapted from the MIT-licensed Bobboteck JoyStick project
  (© 2015 Roberto D'Amico)

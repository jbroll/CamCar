# Design: Web OTA Updates + Hamburger Config Dialog

Date: 2026-06-04
Status: Approved (pending spec review)

Two coordinated changes that meet at one point — the new config dialog is the
home for the web-OTA upload panel.

- **Feature A — Web OTA updates** (firmware): flash new firmware over WiFi via a
  password-protected `POST /update` endpoint. No serial/USB tether needed after
  the one-time partition switch.
- **Feature B — Hamburger config dialog** (UI): move "set and forget" camera
  config off the drive screen into a modal opened from a top-right ☰.

## Goals / non-goals

- **Goal:** wireless firmware updates from both a browser (field) and `make`
  (dev), with one mechanism and one auth scheme.
- **Goal:** declutter the drive screen to joysticks + live controls + telemetry.
- **Non-goal:** ArduinoOTA / IDE network-port push. A web endpoint hit by curl
  replaces it (see "Rejected: ArduinoOTA push").
- **Non-goal:** HTTP-pull / fleet update from a remote URL. One car, YAGNI.
- **Non-goal:** any change to the live `/CarInput` WS protocol. The UI reorg
  moves elements; their command logic is unchanged.

---

## Feature A — Web OTA updates

### A1. Partition scheme (prerequisite)

The S3 currently builds with `PartitionScheme=huge_app` — a **single** app slot,
no room for OTA. OTA needs two app slots plus an `otadata` partition recording
which slot is active.

Switch to a **custom dual-OTA table checked into the repo** at
`partitions/camcar_ota_16MB.csv`:

| Partition | Type / subtype | Size   | Purpose                          |
|-----------|----------------|--------|----------------------------------|
| nvs       | data, nvs      | 20 KB  | existing config + WiFi creds     |
| otadata   | data, ota      | 8 KB   | active-slot selector             |
| app0      | app, ota_0     | ~6.5 MB| firmware slot A                  |
| app1      | app, ota_1     | ~6.5 MB| firmware slot B                  |
| coredump  | data, coredump | 64 KB  | crash dumps (optional, nice)     |

No SPIFFS/FAT partition: web assets are PROGMEM, config is NVS — nothing uses a
filesystem. The current binary is ~1.4 MB, so each 6.5 MB slot has large
headroom to grow.

**Build wiring (Makefile):** the S3 FQBN drops `PartitionScheme=huge_app` and the
custom CSV is passed via arduino-cli build properties, e.g.:

```
--build-property build.partitions=camcar_ota_16MB \
--build-property build.custom_partitions_csv=partitions/camcar_ota_16MB.csv
```

(Exact property names verified against esp32 core 3.3.x during implementation;
fall back to copying the CSV into the core's `tools/partitions/` if custom-CSV
properties prove unreliable.)

**One-time cost:** switching the table requires a final **USB flash** to lay it
down. Every flash after that can be wireless.

### A2. `/update` endpoint (firmware)

New header `src/OtaWeb.h`, modeled on `src/PrefEdit.h` (header-only, inline
static members included in exactly one TU — `CamCar.ino`).

- Registers `POST /update` on the existing AsyncWebServer.
- **Auth:** `request->authenticate(user, pass)` (HTTP Basic). Credentials from
  NVS via the existing `Preferences`/`Prefs.h` mechanism, editable at `/config`.
  New keys (e.g. `ota_user`, `ota_pass`) with sane defaults seeded like WiFi
  creds. 401 + `WWW-Authenticate` on failure.
- **Body handling:** AsyncWebServer upload callback streams chunks straight into
  the core `Update` API:
  - first chunk: `Update.begin(UPDATE_SIZE_UNKNOWN)`
  - each chunk: `Update.write(data, len)`; bail on short write
  - final chunk: `Update.end(true)`; verify success
- **Camera pause:** set the existing snapshot-style pause flag for the duration
  so `sendFrame()` isn't fighting flash writes. Clear on completion/abort.
- **Reboot:** on success, return HTTP 200 then schedule a **deferred reboot**
  (reuse `PrefEdit`'s `_rebootAt = millis() + delay` pattern, serviced in
  `loop()`) so the response flushes before the reboot. On failure, return a 4xx
  with `Update.errorString()` and do not reboot.
- **No new dependency:** hand-rolled (~60 lines), not ElegantOTA, to keep the
  zero-extra-libs ethos (only Micro-RTSP is vendored) and match `PrefEdit` style.

### A3. Dev convenience target (Makefile)

```
make upload-ota   →   curl --fail --user $(OTA_USER):$(OTA_PASS) \
                        -F firmware=@build/esp32.esp32.esp32s3/CamCar.ino.bin \
                        http://$(HOST)/update
```

`OTA_USER`/`OTA_PASS`/`HOST` overridable per-invocation. Same endpoint as the
browser path — one code path serves both dev and field.

### A4. Overhead

The `/update` route is **dormant** when not hit: no task, no socket, no per-loop
work — it costs nothing during normal operation, only during an actual upload.

### Rejected: ArduinoOTA push

Considered for the dev loop, then dropped. It would add an always-on UDP
listener (port 3232), an extra lwip socket descriptor, an mDNS service record,
and a per-`loop()` `handle()` call — a small but permanent footprint — solely to
gain the Arduino IDE "network port" convenience. `make upload-ota` curling the
web endpoint delivers the same dev workflow with zero always-on cost and one
fewer code path. Not worth it.

---

## Feature B — Hamburger config dialog

### B1. Main drive screen (after reorg)

Stays on screen: the two joysticks, the **status readout** (fps/KB, uptime,
battery, scan), **headlight** toggle (💡 `lightBtn`), **stop-camera** toggle (⏹
`camBtn`), a **Snapshot** button (view/save), and a new **☰ hamburger** button in
the top-right.

Moves into the dialog: lock-resolution, resolution, quality, FPS, XCLK select,
auto-tune, snapshot-resolution picker — plus the new Firmware Update panel.

### B2. The dialog

An **in-page modal overlay** (not the separate `/config` page) because these
controls send live `/CarInput` WS commands and must share the page's JS context.

- ☰ opens it; ✕ or tap-outside closes it.
- **While open:** video keeps streaming behind a **dimmed scrim** so live tuning
  (resolution/quality/XCLK) is visible in real time. **Joysticks disabled** while
  open to prevent accidental drive.

**Grouped contents:**
- **Camera:** resolution, quality, FPS, lock-resolution toggle
- **Tuning:** XCLK select + auto-tune button
- **Snapshot:** resolution picker (the Snapshot button itself stays on main)
- **Firmware Update:** file `<input>` + Upload button → `POST /update`, with a
  progress bar and a pass/fail status line (uses `fetch` + Basic-auth creds;
  prompt for / remember the password in-session)
- **Links:** `/config` (WiFi/NVS) opened in a new tab

### B3. Files touched

- `webroot/index.html`: restructure `#centerPanel` — move `#resBar`/`#snapshotBar`
  controls + XCLK into a `#configDialog` overlay; keep status + `lightBtn` +
  `camBtn` + snapshot button on the main panel; add the ☰ button and a
  `#configDialog` container with the grouped sections + the OTA panel.
- `webroot/style.css`: modal overlay + scrim styling, hamburger button, dialog
  groups, OTA progress bar.
- `webroot/app.js`: control handlers move with their elements; **logic
  unchanged** (same WS commands). Add open/close handlers, joystick-disable while
  open, and the OTA upload `fetch` (build FormData, set Basic-auth header, drive
  the progress bar, show result).

All `webroot/` edits go through the gen pipeline (`make build` regenerates
`src/gen/`); never hand-edit `src/gen/`.

### B4. No protocol change

The reorg is pure front-end restructuring. The only new firmware is A2's
`/update` endpoint.

---

## Testing

- **Build (no board):** `make build` must compile with the new partition CSV.
- **Partition fit:** confirm the binary fits a slot and the table flashes.
- **OTA round-trip (board):** USB-flash the partition switch once, then
  `make upload-ota` a subsequent build; verify it boots the new firmware and that
  a bad password → 401 and a truncated/garbage upload → clean failure without
  bricking (active slot unchanged until `Update.end(true)` succeeds).
- **UI:** dialog opens/closes, joysticks disabled while open, video still live
  behind scrim, every moved control still drives its WS command, snapshot button
  still works from the main screen.
- Extend `tests/functional.py` where it can reach (`/update` auth: 401 without
  creds; a real `.bin` upload is hardware-only and stays manual).

## Build / rollout order

1. Add partition CSV + Makefile wiring; `make build` green.
2. Add `src/OtaWeb.h` + NVS keys + `make upload-ota`; `make build` green.
3. **One-time USB flash** to install the OTA partition table.
4. UI reorg (`index.html`/`style.css`/`app.js`) + OTA panel.
5. Verify OTA round-trip wirelessly; verify dialog behavior.

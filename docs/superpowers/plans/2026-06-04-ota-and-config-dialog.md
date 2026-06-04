# Web OTA Updates + Hamburger Config Dialog — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add password-protected over-the-WiFi firmware updates (web `POST /update`) and move "set-and-forget" camera config off the drive screen into a hamburger-opened modal dialog.

**Architecture:** Switch the ESP32-S3 build from the single-slot `huge_app` partition scheme to a custom dual-OTA table (one-time USB flash). Add a header-only `OtaWeb` class (modeled on `PrefEdit`) that registers a Basic-auth `POST /update` handler streaming the upload into the core `Update` API, then deferred-reboots. On the UI side, restructure `index.html` so a top-right ☰ opens an in-page modal holding resolution/quality/FPS/XCLK/snapshot-res controls plus a firmware-upload panel; the drive screen keeps only telemetry, headlight, stop-camera, and the snapshot button.

**Tech Stack:** Arduino (esp32 core 3.3.x), ESPAsyncWebServer, esp32 `Update` library, arduino-cli, vanilla JS/CSS, NVS via `Preferences`.

**Verification note:** This is firmware with no host unit-test harness (per CLAUDE.md: "no on-host unit tests"). Classic red-green TDD does not apply. The verification gates per task are: `make build` compiles clean, the live functional suite (`tests/functional.py`) where reachable, and manual on-hardware checks. Both boards are attached during execution (S3 on `/dev/ttyACM0`, AI-Thinker CAM on `/dev/ttyUSB0`); OTA is **S3-only** (the 4 MB CAM keeps its default scheme — out of scope).

---

## File Structure

**Create:**
- `partitions/camcar_ota_16MB.csv` — custom dual-OTA partition table (source of truth).
- `src/OtaWeb.h` — header-only `OtaWeb` class: `/update` endpoint, Basic auth, `Update` streaming, deferred reboot.

**Modify:**
- `Makefile` — S3 FQBN uses the custom partition scheme; add `upload-ota` target; gitignore-safe root copy.
- `.gitignore` — ignore the build-artifact root `partitions.csv`.
- `src/Prefs.h` — add `ota_user` / `ota_pass` config keys.
- `CamCar.ino` — include `OtaWeb.h`, seed default OTA creds, register the endpoint, call `OtaWeb::loop()`.
- `webroot/config.html` — add OTA user/password fields.
- `webroot/index.html` — restructure `#centerPanel`; add ☰ button and `#configDialog` modal.
- `webroot/style.css` — modal/scrim/hamburger styling; joystick-disable rule.
- `webroot/app.js` — dialog open/close, joystick disable + safe-stop, OTA upload (XHR + progress).
- `tests/functional.py` — add a `/update` auth test (401 without creds).

---

## Task 1: Custom dual-OTA partition table + build wiring

**Files:**
- Create: `partitions/camcar_ota_16MB.csv`
- Modify: `Makefile:9-14` (S3 board block), build/upload targets
- Modify: `.gitignore`

- [ ] **Step 1: Write the partition CSV**

Create `partitions/camcar_ota_16MB.csv`. Two 7.9 MB OTA app slots on the 16 MB flash, plus nvs/otadata/coredump. The ~1.4 MB binary fits with huge headroom.

```csv
# CamCar 16MB dual-OTA partition table (ESP32-S3). Two 7.9MB app slots.
# Name,     Type, SubType,  Offset,   Size,     Flags
nvs,        data, nvs,      0x9000,   0x5000,
otadata,    data, ota,      0xe000,   0x2000,
app0,       app,  ota_0,    0x10000,  0x7F0000,
app1,       app,  ota_1,    0x800000, 0x7F0000,
coredump,   data, coredump, 0xff0000, 0x10000,
```

- [ ] **Step 2: Discover the correct custom-partition incantation**

The esp32 core's "custom" scheme reads `partitions.csv` from the sketch directory. Confirm the S3 board exposes a `custom` PartitionScheme option:

Run: `arduino-cli board details --fqbn esp32:esp32:esp32s3 | grep -A30 PartitionScheme`
Expected: a list including `huge_app` and `custom` (label like "Custom"). Note the exact option id (`custom`).

- [ ] **Step 3: Wire the Makefile**

The CSV source of truth lives in `partitions/`; the core needs it as `./partitions.csv` next to `CamCar.ino`. Copy it as a build step and select the custom scheme for S3 only. Edit the S3 branch in `Makefile` (currently lines 9-14):

Change the S3 `BOARD` line from:
```make
  BOARD = esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app
```
to:
```make
  BOARD = esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom
```

Add a `partitions.csv` copy prerequisite. Add near the other `.PHONY`/variable definitions:
```make
# The esp32 core's "custom" PartitionScheme reads ./partitions.csv from the
# sketch dir. Keep the source of truth in partitions/ and copy it in at build
# time (the root copy is a gitignored build artifact). S3 target only; the
# AI-Thinker CAM keeps its default scheme.
PARTITION_SRC := partitions/camcar_ota_16MB.csv
ifeq ($(TARGET),cam)
PARTITION_DEP :=
else
PARTITION_DEP := partitions.csv
endif

partitions.csv: $(PARTITION_SRC)
	cp $(PARTITION_SRC) partitions.csv
```

Make `build` depend on it:
```make
build: gen-sources $(PARTITION_DEP)
	$(ARDUINO_CLI) compile --fqbn $(BOARD) --libraries $(VENDOR_LIBS) -e $(INO_FILE)
```

And extend `clean` to remove the artifact (append to the existing `clean` recipe):
```make
	rm -f partitions.csv
```

- [ ] **Step 4: Ignore the root build artifact**

Add to `.gitignore`:
```
/partitions.csv
```

- [ ] **Step 5: Build and verify the partition table took effect**

Run: `make build`
Expected: compiles clean. Crucially, the program-storage line should reflect the **larger** app slot — with `huge_app` the max was 3145728 bytes; with the custom 7.9 MB slot it should report a much larger maximum (≈ 8323072):

Run: `make build 2>&1 | grep -i "program storage"`
Expected: `Sketch uses N bytes (X%) of program storage space. Maximum is 8323072 bytes.` (max clearly above 3145728).

If the max still reads 3145728, the custom scheme didn't apply — inspect `build/esp32.esp32.esp32s3/CamCar.ino.partitions.bin` against the CSV and adjust (fallback: place the CSV name into the core `tools/partitions/` and use `--build-property build.partitions=camcar_ota_16MB`).

- [ ] **Step 6: Commit**

```bash
git add partitions/camcar_ota_16MB.csv Makefile .gitignore
git commit -m "build: custom 16MB dual-OTA partition scheme for S3"
```

---

## Task 2: `OtaWeb` endpoint + NVS credential keys

**Files:**
- Create: `src/OtaWeb.h`
- Modify: `src/Prefs.h:2-12`
- Modify: `CamCar.ino` (includes, setup wiring, loop)

- [ ] **Step 1: Add OTA credential keys to Prefs**

Edit `src/Prefs.h` — add the two keys before the `nullptr` terminator:
```c
const char* configParams[] = {
    "ssid",
    "password",
    "hostname",
    "resolution",
    "framesize",
    "quality",
    "xclk",
    "fps",
    "ota_user",
    "ota_pass",
    nullptr  // terminator
};
```

- [ ] **Step 2: Create `src/OtaWeb.h`**

Header-only, mirrors `PrefEdit` (inline statics, included in one TU). Streams the upload into `Update`, Basic-auth gated, deferred reboot so the response flushes. Stops the camera at upload start (`setCameraEnabled(false)` is already called from the async task for the `Camera,0` command, so it is safe here) to free the producer during flash writes; the reboot restarts it.

```cpp
#ifndef OTAWEB_H
#define OTAWEB_H

#include <ESPAsyncWebServer.h>
#include <Update.h>
#include "PrefEdit.h"
#include "Camera.h"

// Web firmware update: POST a firmware .bin to /update (HTTP Basic auth).
// Streams the body straight into the esp32 Update API, then deferred-reboots so
// the HTTP response flushes first. Requires a dual-OTA partition table.
class OtaWeb {
public:
    static void begin(AsyncWebServer* server, CameraHandler* camera) {
        _camera = camera;
        server->on("/update", HTTP_POST, handleResult, handleUpload);
    }

    // Call from loop(): performs the post-success reboot once the response has
    // had time to flush (same pattern as PrefEdit::loop()).
    static void loop() {
        if (_rebootAt != 0 && millis() >= _rebootAt) {
            ESP.restart();
        }
    }

private:
    static constexpr unsigned long REBOOT_DELAY_MS = 1500;
    static CameraHandler* _camera;
    static unsigned long _rebootAt;

    static bool authed(AsyncWebServerRequest* request) {
        String user = PrefEdit::get("ota_user", "admin");
        String pass = PrefEdit::get("ota_pass", "camcar");
        return request->authenticate(user.c_str(), pass.c_str());
    }

    // Runs after the whole body is received: report success/failure, schedule
    // the reboot on success.
    static void handleResult(AsyncWebServerRequest* request) {
        if (!authed(request)) {
            return request->requestAuthentication();
        }
        bool ok = !Update.hasError();
        AsyncWebServerResponse* resp = request->beginResponse(
            ok ? 200 : 500, "text/plain",
            ok ? String("Update OK -- rebooting") : String(Update.errorString()));
        resp->addHeader("Connection", "close");
        request->send(resp);
        if (ok) {
            _rebootAt = millis() + REBOOT_DELAY_MS;
        }
    }

    // Streams each upload chunk into Update. Auth is checked once at index 0 so
    // an unauthorized client never touches flash (handleResult sends the 401).
    static void handleUpload(AsyncWebServerRequest* request, String filename,
                             size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
            if (!authed(request)) {
                return;
            }
            Serial.printf("[ota] start: %s\n", filename.c_str());
            if (_camera) {
                _camera->setCameraEnabled(false);  // free the producer during flash
            }
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
            }
        }
        if (Update.isRunning()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
            if (final) {
                if (Update.end(true)) {
                    Serial.printf("[ota] success: %u bytes\n",
                                  (unsigned)(index + len));
                } else {
                    Update.printError(Serial);
                }
            }
        }
    }
};

CameraHandler* OtaWeb::_camera = nullptr;
unsigned long OtaWeb::_rebootAt = 0;

#endif
```

- [ ] **Step 3: Wire `OtaWeb` into `CamCar.ino`**

Add the include after the existing `#include "src/WebHandler.h"` (line 20):
```cpp
#include "src/OtaWeb.h"
```

In `setup()`, right after `PrefEdit::begin(&server, "/config", configParams);` (line 312), seed default OTA credentials on first boot and register the endpoint:
```cpp
  // First boot: seed default OTA credentials (changeable at /config). The
  // /update endpoint is Basic-auth gated with these.
  if (PrefEdit::get("ota_user").length() == 0) PrefEdit::set("ota_user", "admin");
  if (PrefEdit::get("ota_pass").length() == 0) PrefEdit::set("ota_pass", "camcar");
  OtaWeb::begin(&server, &camera);
```

In `loop()`, add next to `PrefEdit::loop();` (line 403):
```cpp
  OtaWeb::loop();
```

- [ ] **Step 4: Build**

Run: `make build`
Expected: compiles clean (no errors about `Update`, `OtaWeb`, or `authenticate`).

- [ ] **Step 5: Commit**

```bash
git add src/OtaWeb.h src/Prefs.h CamCar.ino
git commit -m "feat: web OTA /update endpoint (Basic auth, deferred reboot)"
```

---

## Task 3: `make upload-ota` convenience target

**Files:**
- Modify: `Makefile` (new target + vars)

- [ ] **Step 1: Add the target**

Append to `Makefile`. Builds first, then curls the freshly built S3 binary at the live host using the OTA creds:
```make
# Wireless firmware update over WiFi: build, then POST the binary to the live
# board's /update endpoint (Basic auth). Override creds/host as needed, e.g.
#   make upload-ota HOST=camcar-f0f5bd.local OTA_PASS=secret
OTA_USER ?= admin
OTA_PASS ?= camcar
S3_BIN := build/esp32.esp32.esp32s3/CamCar.ino.bin

upload-ota: build
	curl --fail --progress-bar --user $(OTA_USER):$(OTA_PASS) \
	  -F firmware=@$(S3_BIN) http://$(HOST)/update
	@echo "\nFirmware posted; board rebooting into new image."
```

Add `upload-ota` to the `.PHONY` line.

- [ ] **Step 2: Verify the target parses (no board needed)**

Run: `make -n upload-ota`
Expected: prints the build prerequisites + the `curl ... /update` command without executing the curl.

- [ ] **Step 3: Commit**

```bash
git add Makefile
git commit -m "build: add make upload-ota (wireless flash via /update)"
```

---

## Task 4: One-time USB flash of the OTA partition table (hardware)

This is the one mandatory wired step; every flash afterward can be wireless. S3 is on `/dev/ttyACM0`.

- [ ] **Step 1: Build is current**

Run: `make build`
Expected: clean, max storage ≈ 8323072 (from Task 1 Step 5).

- [ ] **Step 2: USB-flash the S3 (lays down the new partition table)**

Run: `make upload PORT=/dev/ttyACM0`
Expected: `arduino-cli upload` succeeds (writes bootloader + partitions + app).

- [ ] **Step 3: Confirm the board boots and rejoins WiFi**

Wait ~15 s, then ping the host (no serial — opening serial resets the board):
Run: `ping -c 3 $(HOST 2>/dev/null || echo camcar-f0f5bd.local)` — i.e. `ping -c 3 camcar-f0f5bd.local`
Expected: replies. (If the hostname differs, discover via `arduino-cli board list` / router, or the user's known host.)

- [ ] **Step 4: Confirm the endpoint exists and is auth-gated**

Run: `curl -s -o /dev/null -w "%{http_code}\n" -X POST http://camcar-f0f5bd.local/update`
Expected: `401`.

Run: `curl -s -o /dev/null -w "%{http_code}\n" -X POST --user admin:camcar -F firmware=@/dev/null http://camcar-f0f5bd.local/update`
Expected: a non-401 (`500` is fine — empty body fails `Update`, proving auth passed). No reboot should occur on failure.

No commit (hardware action only).

---

## Task 5: OTA round-trip over WiFi (hardware)

Proves the whole mechanism end-to-end on the now-OTA-capable board.

- [ ] **Step 1: Make a visible, harmless change to confirm the new image runs**

Edit `CamCar.ino` `setup()` boot banner (line 301) from:
```cpp
  Serial.printf("BEGIN\n");
```
to:
```cpp
  Serial.printf("BEGIN (ota build 1)\n");
```

- [ ] **Step 2: Flash it wirelessly**

Run: `make upload-ota HOST=camcar-f0f5bd.local`
Expected: curl progress bar to 100%, server responds `Update OK -- rebooting`, target returns success.

- [ ] **Step 3: Confirm the board came back**

Wait ~15 s.
Run: `curl -s -o /dev/null -w "%{http_code}\n" http://camcar-f0f5bd.local/`
Expected: `200` (UI served → new image booted successfully).

- [ ] **Step 4: Revert the banner change and commit nothing (or keep as marker)**

Edit the banner back to `Serial.printf("BEGIN\n");`. This task validates the pipeline; no source change should persist.

Run: `git diff --stat`
Expected: no changes (banner reverted).

---

## Task 6: Restructure `index.html` — hamburger + config dialog

**Files:**
- Modify: `webroot/index.html`

- [ ] **Step 1: Replace the `#controlsContainer` block**

Replace lines 14-77 (the entire `<div id="controlsContainer">…</div>`) with the version below. The drive screen keeps: status readout, stop-camera, headlight, snapshot view/save, plus a ☰ button. Resolution/quality/FPS/XCLK/auto-tune/lock/snapshot-res move into `#configDialog`. All element **IDs are unchanged** so `app.js` handlers still bind.

```html
    <button id="menuBtn" type="button" title="Settings">&#9776;</button>

    <div id="controlsContainer">
        <div id="motionJoystick" class="joystick-container">
            <div class="joystick-base"></div>
            <div class="joystick-thumb"></div>
        </div>

        <div id="centerPanel">
            <div id="connectionStatus">
                <span id="streamStats">--</span>
                <span id="uptime">up --</span>
                <span id="battery">bat --</span>
                <span id="scan"></span>
            </div>
            <div id="mainBar">
                <button id="camBtn" type="button" title="Stop the camera (clears WiFi for solid control); tap again to restart">&#9209;</button>
                <button id="lightBtn" type="button" title="Headlight on/off">&#128161;</button>
                <button id="snapView" type="button" title="View snapshot in new tab">&#128247;</button>
                <button id="snapSave" type="button" title="Download snapshot">&#11015;</button>
            </div>
        </div>

        <div id="cameraPanTilt" class="joystick-container">
            <div class="joystick-base"></div>
            <div class="joystick-thumb"></div>
        </div>
    </div>

    <div id="configOverlay" hidden>
        <div id="configDialog">
            <div class="cfg-head">
                <span>Settings</span>
                <button id="cfgClose" type="button" title="Close">&#10005;</button>
            </div>

            <div class="cfg-group">
                <h3>Camera</h3>
                <button id="lockBtn" type="button" title="Lock resolution (disable auto-adapt)">&#128275;</button>
                <select id="resolutionSelect" title="Camera resolution (max)">
                    <option value="0">320x240</option>
                    <option value="1">400x296</option>
                    <option value="2" selected>640x480</option>
                    <option value="3">800x600</option>
                    <option value="4">1024x768</option>
                </select>
                <select id="qualitySelect" title="JPEG quality (lower q = sharper, more bandwidth)">
                    <option value="8">Q: High</option>
                    <option value="12" selected>Q: Normal</option>
                    <option value="18">Q: Medium</option>
                    <option value="26">Q: Low</option>
                </select>
                <select id="fpsSelect" title="Max frame rate. Lower it on a marginal link for smooth, low-latency video; raise it on a strong link. Persisted.">
                    <option value="1">FPS 1</option>
                    <option value="2">FPS 2</option>
                    <option value="3">FPS 3</option>
                    <option value="4">FPS 4</option>
                    <option value="5">FPS 5</option>
                    <option value="8">FPS 8</option>
                    <option value="10">FPS 10</option>
                    <option value="12">FPS 12</option>
                    <option value="15">FPS 15</option>
                    <option value="20">FPS 20</option>
                    <option value="30" selected>FPS 30</option>
                </select>
            </div>

            <div class="cfg-group">
                <h3>Tuning</h3>
                <select id="xclkSelect" title="Camera clock (XCLK MHz). Higher = more fps but can disturb 2.4GHz WiFi; clean values are per-board (use Auto-tune, or watch the fps). 8 is always safe. Persisted; boots safe at 8 if unset."></select>
                <button id="tuneBtn" type="button" title="Auto-tune XCLK: scans frequencies and picks the fastest clean one for your WiFi (~40s)">&#128269;</button>
            </div>

            <div class="cfg-group">
                <h3>Snapshot</h3>
                <select id="snapResSelect" title="Snapshot resolution">
                    <option value="0">640x480</option>
                    <option value="1">800x600</option>
                    <option value="2">1024x768</option>
                    <option value="3">1280x1024</option>
                    <option value="4" selected>1600x1200</option>
                </select>
            </div>

            <div class="cfg-group">
                <h3>Firmware Update</h3>
                <input id="otaFile" type="file" accept=".bin">
                <button id="otaBtn" type="button">Upload</button>
                <progress id="otaBar" value="0" max="100"></progress>
                <span id="otaStatus"></span>
            </div>

            <div class="cfg-group">
                <a href="/config" target="_blank">WiFi / advanced config &#8599;</a>
            </div>
        </div>
    </div>
```

- [ ] **Step 2: Regenerate assets and build**

Run: `make build`
Expected: clean compile (the gen pipeline re-embeds the new `index.html`).

- [ ] **Step 3: Commit**

```bash
git add webroot/index.html
git commit -m "ui: move camera config behind a hamburger dialog; OTA panel"
```

---

## Task 7: Modal + hamburger styling

**Files:**
- Modify: `webroot/style.css`

- [ ] **Step 1: Append styles**

Add to the end of `webroot/style.css`. Reuses the existing green-on-black touch aesthetic. Renames the `#resBar/#snapshotBar` selectors to also cover `#mainBar` and the dialog groups; adds the overlay/scrim, hamburger, and joystick-disable rule.

```css
/* Main on-screen control bar (stop-camera, headlight, snapshot). */
#mainBar {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 4px;
}

#mainBar button {
    background: rgba(0, 0, 0, 0.6);
    color: #0f0;
    border: 1px solid #0f0;
    font: 22px monospace;
    padding: 8px 12px;
    border-radius: 5px;
    min-height: 44px;
    box-sizing: border-box;
}

/* Hamburger: fixed top-right, above the video. */
#menuBtn {
    position: fixed;
    top: 8px;
    right: 8px;
    z-index: 20;
    background: rgba(0, 0, 0, 0.6);
    color: #0f0;
    border: 1px solid #0f0;
    font: 22px monospace;
    padding: 6px 12px;
    min-height: 44px;
    border-radius: 5px;
}

/* Config modal: dimmed scrim over the still-live video. */
#configOverlay {
    position: fixed;
    inset: 0;
    z-index: 30;
    background: rgba(0, 0, 0, 0.6);
    display: flex;
    justify-content: center;
    align-items: flex-start;
    overflow-y: auto;
}

#configOverlay[hidden] {
    display: none;
}

#configDialog {
    margin: 6vh 0;
    width: min(92vw, 460px);
    background: #111;
    border: 1px solid #0f0;
    border-radius: 8px;
    padding: 12px;
    box-sizing: border-box;
    color: #0f0;
    font-family: monospace;
}

.cfg-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 18px;
    margin-bottom: 8px;
}

.cfg-group {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 6px;
    padding: 8px 0;
    border-top: 1px solid rgba(0, 255, 0, 0.25);
}

.cfg-group h3 {
    width: 100%;
    margin: 0 0 4px;
    font-size: 13px;
    color: #6f6;
    font-weight: normal;
}

#configDialog select,
#configDialog button,
#configDialog input[type="file"] {
    background: rgba(0, 0, 0, 0.6);
    color: #0f0;
    border: 1px solid #0f0;
    font: 18px monospace;
    padding: 8px 10px;
    border-radius: 5px;
    min-height: 44px;
    box-sizing: border-box;
}

#cfgClose {
    min-height: 36px;
    padding: 4px 10px;
}

#configDialog a {
    color: #6cf;
}

#otaBar {
    width: 100%;
    height: 16px;
}

#otaStatus {
    width: 100%;
    font-size: 13px;
    color: #6f6;
}

/* Active toggles in either location. */
#lockBtn.active,
#lightBtn.active {
    background: rgba(0, 255, 0, 0.85);
    color: #000;
}

/* Disable the joysticks while the dialog is open (avoid accidental drive). */
body.dialog-open .joystick-container {
    pointer-events: none;
    opacity: 0.4;
}
```

Note: the original `#resBar, #snapshotBar` rules (lines 118-145) reference elements that no longer exist as those containers; they are now superseded by the `#mainBar` and `#configDialog` rules above. Leave the old rules in place (harmless dead selectors) **or** delete the `#resBar`/`#snapshotBar` blocks (lines 118-138) since those IDs are gone — delete them for cleanliness.

- [ ] **Step 2: Delete the now-dead `#resBar`/`#snapshotBar` style blocks**

Remove lines 118-138 of the original `style.css` (the `#resBar, #snapshotBar { … }` and the combined `#resBar select, … { … }` rules). Keep the `.active` block (it is re-declared above; either copy is fine — ensure exactly one remains).

- [ ] **Step 3: Build**

Run: `make build`
Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add webroot/style.css
git commit -m "ui: style config modal, hamburger, joystick-disable"
```

---

## Task 8: Dialog behavior + OTA upload in `app.js`

**Files:**
- Modify: `webroot/app.js`

- [ ] **Step 1: Add dialog open/close + safe-stop + OTA upload**

Append the following inside `window.onload`, just before its closing `};` (after the existing snapshot handlers, line ~229). The moved controls' existing handlers are untouched (their IDs are unchanged). This adds: open/close wiring, joystick disable via `body.dialog-open`, a safe-stop on open, and the XHR firmware upload with a progress bar.

```javascript
    // ---- Config dialog (hamburger) ----
    var overlay = document.getElementById("configOverlay");
    function openDialog() {
        // Safe-stop on open so a held joystick command doesn't persist while
        // the joysticks are disabled behind the dialog.
        if (websocketCarInput) { websocketCarInput.send("tank 0 0"); websocketCarInput.send("camr 0 0"); }
        document.body.classList.add("dialog-open");
        overlay.hidden = false;
    }
    function closeDialog() {
        overlay.hidden = true;
        document.body.classList.remove("dialog-open");
    }
    document.getElementById("menuBtn").addEventListener("click", openDialog);
    document.getElementById("cfgClose").addEventListener("click", closeDialog);
    overlay.addEventListener("click", function (e) {
        if (e.target === overlay) closeDialog();   // tap the scrim to close
    });

    // ---- Firmware (OTA) upload ----
    document.getElementById("otaBtn").addEventListener("click", function () {
        var file = document.getElementById("otaFile").files[0];
        var status = document.getElementById("otaStatus");
        var bar = document.getElementById("otaBar");
        if (!file) { status.textContent = "Choose a .bin first"; return; }
        var user = sessionStorage.getItem("otaUser") || prompt("Firmware user", "admin");
        var pass = sessionStorage.getItem("otaPass") || prompt("Firmware password");
        if (!user || !pass) return;
        sessionStorage.setItem("otaUser", user);
        sessionStorage.setItem("otaPass", pass);

        var fd = new FormData();
        fd.append("firmware", file, file.name);
        var xhr = new XMLHttpRequest();
        xhr.open("POST", "/update", true);
        xhr.setRequestHeader("Authorization", "Basic " + btoa(user + ":" + pass));
        xhr.upload.onprogress = function (e) {
            if (e.lengthComputable) {
                var pct = Math.round(e.loaded / e.total * 100);
                bar.value = pct;
                status.textContent = "Uploading " + pct + "%";
            }
        };
        xhr.onload = function () {
            if (xhr.status === 200) {
                status.textContent = "OK — rebooting…";
            } else if (xhr.status === 401) {
                status.textContent = "Auth failed";
                sessionStorage.removeItem("otaUser");
                sessionStorage.removeItem("otaPass");
            } else {
                status.textContent = "Failed: " + (xhr.responseText || xhr.status);
            }
        };
        xhr.onerror = function () { status.textContent = "Upload error"; };
        status.textContent = "Starting…";
        bar.value = 0;
        xhr.send(fd);
    });
```

- [ ] **Step 2: Build**

Run: `make build`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add webroot/app.js
git commit -m "ui: dialog open/close, joystick disable, OTA upload (XHR + progress)"
```

---

## Task 9: OTA credential fields on `/config`

**Files:**
- Modify: `webroot/config.html`

- [ ] **Step 1: Add the OTA fields**

Edit `webroot/config.html` — add before the `<p><small>…</small></p>` note (line 29):
```html
        <p>
            <label>OTA User:</label>
            <input type="text" name="ota_user" value="%ota_user%">
        </p>
        <p>
            <label>OTA Password:</label>
            <input type="password" name="ota_pass" value="%ota_pass%">
        </p>
```

- [ ] **Step 2: Build**

Run: `make build`
Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add webroot/config.html
git commit -m "ui: edit OTA credentials on /config"
```

---

## Task 10: Functional test for `/update` auth

**Files:**
- Modify: `tests/functional.py`

- [ ] **Step 1: Add an HTTP POST helper + auth test**

In `tests/functional.py`, inside the `Suite` test registration area (alongside `test_web_ui`, around line 352), add a test asserting the endpoint rejects unauthenticated POSTs. Uses stdlib `urllib` (matches the file's no-deps rule):

```python
    def test_ota_auth():
        # POST /update with no credentials must be rejected (401), proving the
        # endpoint is auth-gated and never flashes for anonymous clients.
        url = f"http://{host}/update"
        req = urllib.request.Request(url, data=b"", method="POST")
        try:
            urllib.request.urlopen(req, timeout=8)
            code = 200
        except urllib.error.HTTPError as e:
            code = e.code
        assert code == 401, f"expected 401 without creds, got {code}"
        return "rejected unauthenticated /update (401)"
```

Ensure `import urllib.error` is present near the existing `import urllib.request` (line 36); add it if missing.

Register `test_ota_auth` in the same place the other `test_*` functions are collected/run (follow the existing pattern in `Suite`/`main`).

- [ ] **Step 2: Run against the live board**

Run: `make test HOST=camcar-f0f5bd.local TESTFLAGS=""`
Expected: the suite runs and `test_ota_auth` passes (`rejected unauthenticated /update (401)`). Other tests behave as before.

- [ ] **Step 3: Commit**

```bash
git add tests/functional.py
git commit -m "tests: assert /update rejects unauthenticated POST"
```

---

## Task 11: Manual UI verification on hardware

- [ ] **Step 1: Load the UI and exercise the dialog**

Open `http://camcar-f0f5bd.local/` in a browser (or Playwright). Verify:
- Drive screen shows only: telemetry line, stop-camera, headlight, snapshot view/save, and ☰.
- ☰ opens the modal; video keeps streaming dimmed behind it; joysticks are dimmed/non-interactive.
- Resolution/Quality/FPS/XCLK/auto-tune/lock/snapshot-res all still drive their commands (e.g. change resolution → stream resolution changes; toggle headlight from main bar still works).
- ✕ and scrim-tap both close the dialog; joysticks re-enable.

- [ ] **Step 2: Exercise OTA from the dialog**

In the Firmware Update group, pick `build/esp32.esp32.esp32s3/CamCar.ino.bin`, click Upload, enter `admin`/`camcar`. Verify the progress bar advances to 100%, status shows "OK — rebooting…", and the board returns (UI reloads after ~15 s).

- [ ] **Step 3: Final commit if any docs/notes changed**

```bash
git status   # expect clean; commit only if intentional changes remain
```

---

## Self-Review

- **Spec coverage:** A1 partition → Task 1; A2 `/update` endpoint → Task 2; A3 `make upload-ota` → Task 3; one-time USB flash → Task 4; OTA round-trip → Task 5; main-screen contents (status/headlight/stop-cam/snapshot) + dialog groups + live-video-dimmed/joystick-disable → Tasks 6-8; OTA creds editable at /config → Task 9; auth test → Task 10; rejected ArduinoOTA push → correctly absent. All spec sections covered.
- **Type/name consistency:** `OtaWeb::begin(server, camera)` / `OtaWeb::loop()` match between Task 2 definition and CamCar.ino wiring; `ota_user`/`ota_pass` keys consistent across Prefs.h, OtaWeb.h defaults, config.html, and seeding; element IDs (`menuBtn`, `configOverlay`, `cfgClose`, `otaFile`, `otaBtn`, `otaBar`, `otaStatus`, `lockBtn`, `resolutionSelect`, …) consistent across index.html, style.css, app.js.
- **CAM target:** OTA scoped S3-only; `PARTITION_DEP` empty for `TARGET=cam`, so the CAM build is unaffected.
- **Placeholders:** none — every code/step is concrete.

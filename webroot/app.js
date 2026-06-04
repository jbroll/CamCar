// CamCar web UI glue: wires the joysticks, the auto-reconnecting camera/control
// sockets, the stream-stats overlay, and the resolution/snapshot controls.
// Depends on joystick.js (Joystick) and socket.js (ReconnectingSocket).

var webSocketCameraUrl = "ws://" + window.location.hostname + "/Camera";
var webSocketCarInputUrl = "ws://" + window.location.hostname + "/CarInput";
var websocketCamera = null;
var websocketCarInput = null;
var statFrames = 0, statBytes = 0, lastFrameMs = 0;
var connState = { cam: false, ctl: false };
var deviceUptimeSec = null, uptimeSyncMs = 0;  // device uptime, resynced from status frames

function renderConn() {
    // No status text -- the connection state just tints the whole status line:
    // green when fully connected, red when the camera or control link is down.
    var el = document.getElementById('connectionStatus');
    if (el) el.classList.toggle('disconnected', !(connState.cam && connState.ctl));
}

// Stream stats overlay (measured in-browser; no serial needed).
function updateStreamStats() {
    var kb = statFrames ? (statBytes / statFrames / 1024) : 0;
    // Resolution is shown in the dropdown now (synced from the "res" status
    // frame), so the status line is just fps + bytes/frame.
    document.getElementById("streamStats").textContent =
        statFrames + " fps   " + kb.toFixed(1) + " KB/f";
    statFrames = 0;
    statBytes = 0;
}

// Text status frames from the device (sent on the camera socket alongside the
// binary JPEG frames). Currently just "up <seconds>" device uptime.
function handleStatus(msg) {
    if (msg.indexOf("up ") === 0) {
        deviceUptimeSec = parseInt(msg.slice(3), 10);
        uptimeSyncMs = Date.now();
    } else if (msg.indexOf("bat ") === 0) {
        var p = msg.slice(4).split(" ");   // "<volts> <percent>"
        var el = document.getElementById("battery");
        if (el) el.textContent = "bat " + p[0] + "V" + (p[1] !== undefined ? " (" + p[1] + "%)" : "");
    } else if (msg.indexOf("xclk ") === 0) {
        // Keep the menu showing the device's actual XCLK (e.g. a persisted value
        // after reboot). Only adopt it if a matching option exists.
        var v = msg.slice(5).trim();
        var sel = document.getElementById("xclkSelect");
        if (sel && sel.value !== v) {
            for (var i = 0; i < sel.options.length; i++) {
                if (sel.options[i].value === v) { sel.value = v; break; }
            }
        }
    } else if (msg.indexOf("res ") === 0) {
        // Show the current (possibly auto-adapted) resolution in the dropdown.
        var rv = msg.slice(4).trim();
        var rsel = document.getElementById("resolutionSelect");
        if (rsel && rsel.value !== rv) rsel.value = rv;
    } else if (msg.indexOf("fps ") === 0) {
        var fv = msg.slice(4).trim();
        var fsel = document.getElementById("fpsSelect");
        if (fsel && fsel.value !== fv) {
            for (var fi = 0; fi < fsel.options.length; fi++) {
                if (fsel.options[fi].value === fv) { fsel.value = fv; break; }
            }
        }
    } else if (msg.indexOf("scanstart") === 0) {
        var s0 = document.getElementById("scan"); if (s0) s0.textContent = "tuning XCLK…";
    } else if (msg.indexOf("scanbest ") === 0) {
        var best = msg.slice(9).trim();
        var sb = document.getElementById("xclkSelect");
        if (sb) for (var j = 0; j < sb.options.length; j++) {
            if (sb.options[j].value === best) { sb.value = best; break; }
        }
        var s1 = document.getElementById("scan"); if (s1) s1.textContent = "tuned → XCLK " + best + " MHz";
    } else if (msg.indexOf("scan ") === 0) {
        var p2 = msg.slice(5).split(" ");   // "<mhz> <fps>"
        var s2 = document.getElementById("scan");
        if (s2) s2.textContent = "tuning " + p2[0] + " MHz: " + p2[1] + " fps";
    }
}

function fmtUptime(s) {
    var d = Math.floor(s / 86400); s -= d * 86400;
    var h = Math.floor(s / 3600);  s -= h * 3600;
    var m = Math.floor(s / 60);    s -= m * 60;
    function p(n) { return (n < 10 ? "0" : "") + n; }
    return (d > 0 ? d + "d " : "") + p(h) + ":" + p(m) + ":" + p(s);
}

// Tick locally each second; the device resyncs us every couple of seconds.
function renderUptime() {
    var el = document.getElementById("uptime");
    if (deviceUptimeSec === null) { el.textContent = "up --"; return; }
    var live = deviceUptimeSec + Math.floor((Date.now() - uptimeSyncMs) / 1000);
    el.textContent = "up " + fmtUptime(live);
}

// Joysticks: motion -> tank drive, camera -> pan/tilt.
const motionJoystick = new Joystick(
    document.getElementById('motionJoystick'),
    (x, y) => { if (websocketCarInput) websocketCarInput.send(`tank ${x} ${y}`); }
);
const cameraPanTilt = new Joystick(
    document.getElementById('cameraPanTilt'),
    (x, y) => { if (websocketCarInput) websocketCarInput.send(`camr ${x} ${y}`); }
);

window.onload = function () {
    var cameraImage = document.getElementById("cameraImage");
    var prevUrl = null;
    websocketCamera = new ReconnectingSocket(webSocketCameraUrl, {
        onopen: function () { connState.cam = true; renderConn(); },
        onclose: function () { connState.cam = false; renderConn(); },
        onmessage: function (event) {
            if (typeof event.data === "string") { handleStatus(event.data); return; }
            lastFrameMs = Date.now();
            statFrames++;
            statBytes += event.data.size;
            var url = URL.createObjectURL(event.data);
            cameraImage.src = url;
            if (prevUrl) URL.revokeObjectURL(prevUrl);  // avoid blob leak
            prevUrl = url;
        }
    });
    websocketCarInput = new ReconnectingSocket(webSocketCarInputUrl, {
        onopen: function () { connState.ctl = true; renderConn(); },
        onclose: function () { connState.ctl = false; renderConn(); }
    });
    websocketCamera.connect();
    websocketCarInput.connect();

    // Watchdog: a half-open socket may never fire onclose, so if the stream goes
    // silent longer than a UXGA snapshot pause, force a reconnect.
    setInterval(function () {
        if (websocketCamera && websocketCamera.readyState === WebSocket.OPEN
            && lastFrameMs && (Date.now() - lastFrameMs) > 6000) {
            lastFrameMs = Date.now();
            websocketCamera.reconnectNow();
        }
    }, 2000);

    // Reconnect promptly when the network returns or the app is resumed (mobile
    // backgrounding often kills the socket without an onclose).
    function reconnectAll() {
        if (websocketCamera) websocketCamera.reconnectNow();
        if (websocketCarInput) websocketCarInput.reconnectNow();
    }
    window.addEventListener('online', reconnectAll);
    document.addEventListener('visibilitychange', function () {
        if (document.visibilityState === 'visible') reconnectAll();
    });

    setInterval(updateStreamStats, 1000);
    setInterval(renderUptime, 1000);

    document.getElementById("resolutionSelect").addEventListener("change", function () {
        if (websocketCarInput) websocketCarInput.send("Resolution," + this.value);
    });

    document.getElementById("qualitySelect").addEventListener("change", function () {
        if (websocketCarInput) websocketCarInput.send("Quality," + this.value);
    });

    document.getElementById("fpsSelect").addEventListener("change", function () {
        if (websocketCarInput) websocketCarInput.send("Fps," + this.value);
    });

    // Populate the XCLK menu at 0.5 MHz steps (8.0 .. 20.0); the device's
    // current value arrives via the "xclk" status frame and selects the match.
    var xsel = document.getElementById("xclkSelect");
    for (var f = 8; f <= 20.0001; f += 0.5) {
        var v = f.toFixed(1);
        var o = document.createElement("option");
        o.value = v; o.textContent = "XCLK " + v;
        if (v === "8.0") o.selected = true;
        xsel.appendChild(o);
    }
    xsel.addEventListener("change", function () {
        if (websocketCarInput) websocketCarInput.send("Xclk," + this.value);
    });

    // Auto-tune: ask the firmware to scan XCLKs and adopt the fastest clean one.
    document.getElementById("tuneBtn").addEventListener("click", function () {
        if (websocketCarInput) websocketCarInput.send("XclkScan");
        var s = document.getElementById("scan");
        if (s) s.textContent = "tuning XCLK… (~40s)";
    });

    // Camera stop/start: stopping deinits the camera (XCLK off) so its 2.4GHz
    // radiation clears and control gets solid; tap again to restart.
    var camOn = true;
    document.getElementById("camBtn").addEventListener("click", function () {
        camOn = !camOn;
        this.innerHTML = camOn ? "&#9209;" : "&#9654;";   // stop square / play
        this.classList.toggle("active", !camOn);
        if (websocketCarInput) websocketCarInput.send("Camera," + (camOn ? 1 : 0));
    });

    // Lock toggle: freeze the current resolution (disable auto-adapt). Manual
    // resolution changes still work while locked.
    var locked = false;
    document.getElementById("lockBtn").addEventListener("click", function () {
        locked = !locked;
        this.classList.toggle("active", locked);
        this.innerHTML = locked ? "&#128274;" : "&#128275;";  // closed / open padlock
        if (websocketCarInput) websocketCarInput.send("Lock," + (locked ? 1 : 0));
    });

    // Headlight toggle.
    var lightOn = false;
    document.getElementById("lightBtn").addEventListener("click", function () {
        lightOn = !lightOn;
        this.classList.toggle("active", lightOn);
        if (websocketCarInput) websocketCarInput.send("Light," + (lightOn ? 1 : 0));
    });

    function snapUrl(download) {
        var idx = document.getElementById("snapResSelect").value;
        return "/snapshot?res=" + idx + (download ? "&download=1" : "");
    }
    function doSnapView() { window.open(snapUrl(false), "_blank"); }
    function doSnapSave() {
        var a = document.createElement("a");
        a.href = snapUrl(true);
        a.download = "snapshot.jpg";
        document.body.appendChild(a);
        a.click();
        a.remove();
    }
    // Bound on both the main-screen buttons and their duplicates in the dialog.
    ["snapView", "snapViewCfg"].forEach(function (id) {
        var el = document.getElementById(id);
        if (el) el.addEventListener("click", doSnapView);
    });
    ["snapSave", "snapSaveCfg"].forEach(function (id) {
        var el = document.getElementById(id);
        if (el) el.addEventListener("click", doSnapSave);
    });

    // ---- Config dialog (hamburger) ----
    var overlay = document.getElementById("configOverlay");
    function loadNetSettings() {
        // Pre-fill the Network & Security fields from the device (non-secret
        // values; password fields stay blank = "leave unchanged").
        fetch("/config.json").then(function (r) { return r.json(); }).then(function (c) {
            document.getElementById("cfgHostname").value = c.hostname || "";
            document.getElementById("cfgHostname").placeholder = c.hostname_effective || "camcar-xxxxxx";
            document.getElementById("cfgSsid").value = c.ssid || "";
            document.getElementById("cfgOtaUser").value = c.ota_user || "";
        }).catch(function () {});
    }
    function openDialog() {
        // Safe-stop on open so a held joystick command doesn't persist while
        // the joysticks are disabled behind the dialog.
        if (websocketCarInput) { websocketCarInput.send("tank 0 0"); websocketCarInput.send("camr 0 0"); }
        document.body.classList.add("dialog-open");
        overlay.hidden = false;
        loadNetSettings();
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
    document.addEventListener("keydown", function (e) {
        if (e.key === "Escape" && !overlay.hidden) closeDialog();   // ESC closes
    });

    // ---- Network & Security save (folds the old /config page into the dialog) ----
    document.getElementById("cfgSave").addEventListener("click", function () {
        var status = document.getElementById("cfgStatus");
        // Always send the non-secret fields; send a password only if the user
        // typed one (blank = leave the stored value unchanged). hostname/ssid
        // unchanged from their loaded values are no-ops server-side.
        var params = new URLSearchParams();
        params.set("hostname", document.getElementById("cfgHostname").value.trim());
        params.set("ssid", document.getElementById("cfgSsid").value.trim());
        params.set("ota_user", document.getElementById("cfgOtaUser").value.trim());
        var pw = document.getElementById("cfgPass").value;
        if (pw) params.set("password", pw);
        var opw = document.getElementById("cfgOtaPass").value;
        if (opw) params.set("ota_pass", opw);

        status.textContent = "Saving…";
        fetch("/config", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: params.toString()
        }).then(function (r) { return r.text(); }).then(function (t) {
            status.textContent = (t.indexOf("reboot") >= 0)
                ? "Saved — rebooting to apply WiFi…"
                : "Saved.";
            document.getElementById("cfgPass").value = "";
            document.getElementById("cfgOtaPass").value = "";
        }).catch(function () { status.textContent = "Save failed"; });
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
};

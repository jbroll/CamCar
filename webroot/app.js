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
    var el = document.getElementById('connText');
    if (!el) return;
    if (connState.cam && connState.ctl) { el.textContent = 'Connected'; return; }
    var down = [];
    if (!connState.cam) down.push('Camera');
    if (!connState.ctl) down.push('Controls');
    el.textContent = down.join(' & ') + ' reconnecting…';
}

// Stream stats overlay (measured in-browser; no serial needed).
function updateStreamStats() {
    var kb = statFrames ? (statBytes / statFrames / 1024) : 0;
    var img = document.getElementById("cameraImage");
    var res = img.naturalWidth ? img.naturalWidth + "x" + img.naturalHeight : "--";
    document.getElementById("streamStats").textContent =
        statFrames + " fps   " + res + "   " + kb.toFixed(1) + " KB/f";
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

    document.getElementById("xclkSelect").addEventListener("change", function () {
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
    document.getElementById("snapView").addEventListener("click", function () {
        window.open(snapUrl(false), "_blank");
    });
    document.getElementById("snapSave").addEventListener("click", function () {
        var a = document.createElement("a");
        a.href = snapUrl(true);
        a.download = "snapshot.jpg";
        document.body.appendChild(a);
        a.click();
        a.remove();
    });
};

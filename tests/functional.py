#!/usr/bin/env python3
"""CamCar functional tests — exercise every streaming transport and cycle the
camera through several runtime configurations against a *live* board.

Launched via `make test` (which provisions the venv and passes --host). These
are integration tests against real hardware, not unit tests: they connect to
the board over the network and assert that each protocol delivers valid frames
and that runtime config commands actually take effect (verified by decoding the
JPEG dimensions out of the stream).

Transports covered:
  - web UI            GET  /                       (port 80)
  - WS live view      ws   /Camera                 (port 80)
  - HTTP-MJPEG        GET  :81/stream              (single + multi-client)
  - RTSP / RTP-JPEG   rtsp ://host:554/mjpeg/1     (TCP + UDP, via ffmpeg)
  - snapshot          GET  /snapshot?res=0..4      (port 80)

Config cycling (via the /CarInput WS control channel):
  - Resolution ladder 0..4 — verified by decoding stream-frame dimensions
  - Quality            — verified by frame validity + size trend
  - Fps                — verified by measured frame rate
  - Xclk (opt-in)      — verified by frames still flowing; restored after

Self-contained except for ffmpeg (RTSP tests skip cleanly if it is absent).
"""

import argparse
import base64
import os
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.request
import urllib.error
from concurrent.futures import ThreadPoolExecutor

# ---- Stream resolution ladder (must match src/Camera.cpp RES_LADDER) --------
RES_LADDER = [(320, 240), (400, 296), (640, 480), (800, 600), (1024, 768)]
# ---- Snapshot ladder (must match src/Camera.cpp SNAP_LADDER) ----------------
SNAP_LADDER = [(640, 480), (800, 600), (1024, 768), (1280, 1024), (1600, 1200)]

# Restore-to-defaults after a run (leave the board in a sane streaming state).
DEFAULT_RES_INDEX = 2     # VGA
DEFAULT_QUALITY = 12
DEFAULT_FPS = 30


# ---------------------------------------------------------------------------
# JPEG helpers
# ---------------------------------------------------------------------------
def jpeg_dimensions(data: bytes):
    """Return (width, height) from a JPEG's SOF marker, or None if not found."""
    SOF = {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
           0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}
    i = 2  # skip SOI (FFD8)
    n = len(data)
    while i + 9 < n:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in SOF:
            height = (data[i + 5] << 8) | data[i + 6]
            width = (data[i + 7] << 8) | data[i + 8]
            return width, height
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        seglen = (data[i + 2] << 8) | data[i + 3]
        i += 2 + seglen
    return None


def is_jpeg(data: bytes) -> bool:
    return len(data) > 4 and data[:2] == b"\xff\xd8" and data.rstrip(b"\x00")[-2:] == b"\xff\xd9"


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------
def http_get(host, path, port=80, timeout=8):
    url = f"http://{host}:{port}{path}"
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, dict(r.headers), r.read()


def mjpeg_frames(host, duration, port=81, path="/stream", timeout=12):
    """Connect to an MJPEG endpoint and yield raw JPEG frames for `duration`s.

    Parses multipart/x-mixed-replace by reading each part's Content-Length.
    """
    deadline = time.time() + duration
    s = socket.create_connection((host, port), timeout=timeout)
    s.settimeout(timeout)
    s.sendall(f"GET {path} HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
    buf = b""

    def fill(minlen):
        nonlocal buf
        while len(buf) < minlen:
            chunk = s.recv(65536)
            if not chunk:
                raise ConnectionError("stream closed")
            buf += chunk

    # consume HTTP response headers
    while b"\r\n\r\n" not in buf:
        buf += s.recv(4096)
    buf = buf.split(b"\r\n\r\n", 1)[1]

    try:
        while time.time() < deadline:
            # read until the full part header (ending in a blank line) is
            # buffered — not just Content-Length:, which can arrive before the
            # terminating CRLFCRLF and would make .index() raise.
            while b"\r\n\r\n" not in buf:
                fill(len(buf) + 1)
            hdr_end = buf.index(b"\r\n\r\n")
            header = buf[:hdr_end].decode(errors="replace")
            clen = None
            for ln in header.split("\r\n"):
                if ln.lower().startswith("content-length:"):
                    clen = int(ln.split(":", 1)[1])
            buf = buf[hdr_end + 4:]
            if clen is None:
                break
            fill(clen)
            yield buf[:clen]
            buf = buf[clen:]
    finally:
        s.close()


def count_mjpeg(host, duration, port=81):
    frames = list(mjpeg_frames(host, duration, port=port))
    return frames


def sample_frames(host, duration, attempts=3, port=81):
    """count_mjpeg with retries — the board has a limited socket table and is
    often shared (Motion server, browser), so a connect can transiently fail."""
    last = []
    for _ in range(attempts):
        try:
            last = count_mjpeg(host, duration, port=port)
        except (OSError, ConnectionError):
            last = []
        if last:
            return last
        time.sleep(0.6)
    return last


def sample_last_jpeg(host, duration=2.0, attempts=3):
    """Return the most recent *valid* JPEG from :81 (last frame after a config
    change has settled), retrying through transient socket pressure."""
    for _ in range(attempts):
        frames = sample_frames(host, duration, attempts=1)
        good = [f for f in frames if is_jpeg(f)]
        if good:
            return good[-1]
        time.sleep(0.6)
    raise AssertionError("no valid JPEG frames from :81/stream")


# ---------------------------------------------------------------------------
# Minimal WebSocket client (stdlib only — avoids a third-party dependency so
# `make test` needs nothing but python3 + ffmpeg). RFC 6455: client->server
# frames must be masked; server->client frames are not.
# ---------------------------------------------------------------------------
class WS:
    def __init__(self, host, path, port=80, timeout=10):
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.sock.sendall(
            f"GET {path} HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
            f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
            f"Sec-WebSocket-Version: 13\r\n\r\n".encode())
        self.buf = b""
        while b"\r\n\r\n" not in self.buf:
            self.buf += self.sock.recv(4096)
        line = self.buf.split(b"\r\n", 1)[0]
        if b"101" not in line:
            raise ConnectionError(f"WS upgrade failed: {line!r}")
        self.buf = self.buf.split(b"\r\n\r\n", 1)[1]

    def _fill(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("WS closed")
            self.buf += chunk

    def _recv_frame(self):
        """Read one WS frame: return (fin, opcode, payload)."""
        self._fill(2)
        b0, b1 = self.buf[0], self.buf[1]
        fin = b0 >> 7
        opcode = b0 & 0x0F
        ln = b1 & 0x7F
        idx = 2
        if ln == 126:
            self._fill(idx + 2)
            ln = struct.unpack(">H", self.buf[idx:idx + 2])[0]
            idx += 2
        elif ln == 127:
            self._fill(idx + 8)
            ln = struct.unpack(">Q", self.buf[idx:idx + 8])[0]
            idx += 8
        self._fill(idx + ln)
        payload = self.buf[idx:idx + ln]
        self.buf = self.buf[idx + ln:]
        return fin, opcode, payload

    def recv(self):
        """Return (opcode, payload) for a complete message, reassembling
        fragmented frames (the firmware sends each JPEG as a binary frame split
        into FIN=0 continuation frames)."""
        fin, opcode, payload = self._recv_frame()
        if opcode == 0x8:                          # close
            return opcode, payload
        data = bytearray(payload)
        while not fin:
            fin, _cont_op, more = self._recv_frame()
            data += more                           # continuation: opcode 0x0
        return opcode, bytes(data)

    def send_text(self, text):
        data = text.encode()
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        hdr = bytes([0x81])                       # FIN + text opcode
        n = len(data)
        if n < 126:
            hdr += bytes([0x80 | n])              # MASK bit + len
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.sock.sendall(hdr + mask + masked)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def ws_camera_frames(host, count, timeout=10):
    """Collect up to `count` binary JPEG frames from the /Camera WS."""
    frames = []
    deadline = time.time() + timeout
    ws = WS(host, "/Camera", timeout=timeout)
    try:
        while len(frames) < count and time.time() < deadline:
            opcode, payload = ws.recv()
            if opcode == 0x2:                     # binary frame == JPEG
                frames.append(payload)
            elif opcode == 0x8:                   # close
                break
    finally:
        ws.close()
    return frames


class CarInput:
    """Control channel: send /CarInput text commands."""

    def __init__(self, host, timeout=10):
        self.ws = WS(host, "/CarInput", timeout=timeout)

    def send(self, cmd):
        self.ws.send_text(cmd)

    def kv(self, key, value):
        self.ws.send_text(f"{key},{value}")
        time.sleep(0.1)

    def close(self):
        self.ws.close()


# ---------------------------------------------------------------------------
# RTSP (via ffmpeg)
# ---------------------------------------------------------------------------
def rtsp_frame_count(host, transport, duration, kill_after=None):
    url = f"rtsp://{host}:554/mjpeg/1"
    # NB: no -rw_timeout / -stimeout — option names vary across ffmpeg builds
    # ("Option not found" aborts the run). The subprocess timeout below is the
    # hang guard; UDP RTP setup occasionally wedges, so callers retry.
    proc = subprocess.run(
        ["ffmpeg", "-hide_banner", "-rtsp_transport", transport,
         "-i", url, "-t", str(duration), "-f", "null", "-"],
        capture_output=True, text=True, timeout=kill_after or (duration + 45))
    last = 0
    for tok in proc.stderr.split():
        if tok.startswith("frame="):
            rest = tok.split("=", 1)[1]
            if rest.isdigit():
                last = int(rest)
    # ffmpeg also prints "frame=  NN" with a space; scan a fallback
    import re
    m = re.findall(r"frame=\s*(\d+)", proc.stderr)
    if m:
        last = max(last, int(m[-1]))
    return last


# ---------------------------------------------------------------------------
# Test harness
# ---------------------------------------------------------------------------
class Suite:
    def __init__(self):
        self.results = []

    def run(self, name, fn):
        time.sleep(0.7)        # let the board's limited socket table drain
        t0 = time.time()
        try:
            detail = fn()
            ok, msg = True, (detail or "")
        except SkipTest as e:
            ok, msg = None, str(e)
        except Exception as e:
            ok, msg = False, f"{type(e).__name__}: {e}"
        dt = time.time() - t0
        self.results.append((name, ok, msg, dt))
        mark = {True: "PASS", False: "FAIL", None: "SKIP"}[ok]
        print(f"  [{mark}] {name:<28} {msg}  ({dt:.1f}s)")
        return ok

    def summary(self):
        p = sum(1 for _, ok, _, _ in self.results if ok is True)
        f = sum(1 for _, ok, _, _ in self.results if ok is False)
        s = sum(1 for _, ok, _, _ in self.results if ok is None)
        print(f"\n{p} passed, {f} failed, {s} skipped")
        return f == 0


class SkipTest(Exception):
    pass


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
def make_tests(host, args):
    def test_web_ui():
        status, _headers, body = http_get(host, "/")
        assert status == 200, f"status {status}"
        assert b"<" in body[:512], "no HTML"
        return f"{len(body)} bytes"

    def test_ws_camera():
        # /Camera is single-viewer: a competing viewer (e.g. an open browser
        # tab) is cut off when we connect, so the first frame can be torn mid
        # hand-off. Require that *some* collected frame is a clean JPEG.
        frames = ws_camera_frames(host, count=12, timeout=12)
        assert len(frames) >= 5, f"only {len(frames)} frames"
        good = [f for f in frames if is_jpeg(f)]
        assert good, f"{len(frames)} frames but none a valid JPEG"
        dim = jpeg_dimensions(good[-1])
        assert dim, "could not decode JPEG dimensions"
        return f"{len(frames)} frames ({len(good)} clean), {dim[0]}x{dim[1]}"

    def test_mjpeg_single():
        frames = sample_frames(host, args.dur)
        assert len(frames) >= 3, f"only {len(frames)} frames"
        assert is_jpeg(frames[0]), "not a JPEG"
        return f"{len(frames)} frames ({len(frames)/args.dur:.1f} fps)"

    def test_mjpeg_multiclient():
        n = args.clients
        with ThreadPoolExecutor(max_workers=n) as ex:
            futs = [ex.submit(sample_frames, host, args.dur) for _ in range(n)]
            counts = [len(f.result()) for f in futs]
        assert all(c >= 3 for c in counts), f"some clients starved: {counts}"
        fps = [f"{c/args.dur:.0f}" for c in counts]
        return f"{n} clients fps={fps}"

    def test_rtsp_tcp():
        if not shutil.which("ffmpeg"):
            raise SkipTest("ffmpeg not installed")
        n = rtsp_frame_count(host, "tcp", args.dur)
        assert n > 0, "no frames"
        return f"{n} frames ({n/args.dur:.1f} fps)"

    def test_rtsp_udp():
        if not shutil.which("ffmpeg"):
            raise SkipTest("ffmpeg not installed")
        # UDP RTP setup occasionally wedges; cap each try and retry rather than
        # waiting out the long subprocess timeout.
        n = 0
        for _ in range(3):
            try:
                n = rtsp_frame_count(host, "udp", args.dur, kill_after=args.dur + 8)
            except subprocess.TimeoutExpired:
                n = 0
            if n > 0:
                break
            time.sleep(1.0)
        assert n > 0, "no frames (UDP RTP setup kept timing out)"
        return f"{n} frames ({n/args.dur:.1f} fps)"

    def test_rtsp_concurrent():
        if not shutil.which("ffmpeg"):
            raise SkipTest("ffmpeg not installed")
        with ThreadPoolExecutor(max_workers=2) as ex:
            futs = [ex.submit(rtsp_frame_count, host, "tcp", args.dur) for _ in range(2)]
            counts = [f.result() for f in futs]
        assert all(c > 0 for c in counts), f"a session starved: {counts}"
        return f"2 sessions frames={counts}"

    def test_snapshot():
        sizes = []
        for idx, (w, h) in enumerate(SNAP_LADDER):
            status, _headers, body = http_get(host, f"/snapshot?res={idx}", timeout=45)
            assert status == 200, f"res {idx}: status {status}"
            assert is_jpeg(body), f"res {idx}: not a JPEG"
            dim = jpeg_dimensions(body)
            assert dim == (w, h), f"res {idx}: got {dim}, want {w}x{h}"
            sizes.append(len(body))
        return f"5 stills, sizes {[s//1024 for s in sizes]} KB"

    def test_config_resolution():
        car = CarInput(host)
        try:
            car.kv("Lock", 1)          # freeze auto-adapt so the ceiling sticks
            time.sleep(0.3)
            for idx, (w, h) in enumerate(RES_LADDER):
                car.kv("Resolution", idx)
                time.sleep(1.2)        # let the sensor switch
                frame = sample_last_jpeg(host, 1.5)
                dim = jpeg_dimensions(frame)
                assert dim == (w, h), f"index {idx}: stream is {dim}, want {w}x{h}"
        finally:
            car.kv("Resolution", DEFAULT_RES_INDEX)
            car.kv("Lock", 0)
            car.close()
        return "ladder 0..4 verified by decoded frame dims"

    def test_config_quality():
        car = CarInput(host)
        try:
            car.kv("Lock", 1)
            car.kv("Resolution", DEFAULT_RES_INDEX)
            time.sleep(1.0)
            sizes = {}
            for q in (8, 20, 40):
                car.kv("Quality", q)
                time.sleep(0.8)
                frame = sample_last_jpeg(host, 1.5)
                sizes[q] = len(frame)
            # lower q number = higher quality = bigger frame
            assert sizes[8] > sizes[40], f"size trend wrong: {sizes}"
        finally:
            car.kv("Quality", DEFAULT_QUALITY)
            car.kv("Lock", 0)
            car.close()
        return f"q->size {{8:{sizes[8]},20:{sizes[20]},40:{sizes[40]}}} (monotone)"

    def test_config_fps():
        car = CarInput(host)
        try:
            car.kv("Fps", 4)
            time.sleep(0.5)
            slow = len(sample_frames(host, 3.0)) / 3.0
            car.kv("Fps", 30)
            time.sleep(0.5)
            fast = len(sample_frames(host, 3.0)) / 3.0
            assert slow < fast, f"fps cap not honored: slow={slow:.1f} fast={fast:.1f}"
            assert slow <= 7, f"4fps cap leaked: {slow:.1f}"
        finally:
            car.kv("Fps", DEFAULT_FPS)
            car.close()
        return f"4fps->{slow:.1f}, 30fps->{fast:.1f}"

    def test_config_xclk():
        if not args.with_xclk:
            raise SkipTest("use --with-xclk (RF-disruptive, board-specific)")
        car = CarInput(host)
        try:
            for mhz in (8, args.xclk_restore):
                car.kv("Xclk", mhz)
                time.sleep(2.0)
                frames = count_mjpeg(host, 2.0)
                assert len(frames) > 0, f"{mhz}MHz: no frames"
        finally:
            car.kv("Xclk", args.xclk_restore)
            car.close()
        return f"8MHz + {args.xclk_restore}MHz both stream; restored {args.xclk_restore}"

    def test_carinput_drive():
        car = CarInput(host)
        try:
            for cmd in ("tank 0 50", "tank 50 0", "camr 30 -30", "tank 0 0", "camr 0 0"):
                car.send(cmd)
                time.sleep(0.15)
            # still connected + still streaming after the command burst
            assert len(sample_frames(host, 1.5)) > 0, "stream died after drive cmds"
        finally:
            car.send("tank 0 0")
            car.close()
        return "tank/camr accepted, stream alive"

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

    # RTSP tests run LAST: a session teardown can currently reboot the board
    # (see the C1 double-free), and putting them last keeps that from
    # cascading into the healthy HTTP/WS/config tests.
    return [
        ("web_ui", test_web_ui),
        ("ws_camera", test_ws_camera),
        ("mjpeg_single", test_mjpeg_single),
        ("mjpeg_multiclient", test_mjpeg_multiclient),
        ("snapshot_all_res", test_snapshot),
        ("config_resolution", test_config_resolution),
        ("config_quality", test_config_quality),
        ("config_fps", test_config_fps),
        ("config_xclk", test_config_xclk),
        ("carinput_drive", test_carinput_drive),
        ("ota_auth", test_ota_auth),
        ("rtsp_tcp", test_rtsp_tcp),
        ("rtsp_udp", test_rtsp_udp),
        ("rtsp_concurrent", test_rtsp_concurrent),
    ]


def main():
    ap = argparse.ArgumentParser(description="CamCar functional tests (live board)")
    ap.add_argument("--host", default="camcar-f0f5bd.local",
                    help="board hostname/IP (default: camcar-f0f5bd.local)")
    ap.add_argument("--dur", type=float, default=4.0,
                    help="per-stream sample duration in seconds")
    ap.add_argument("--clients", type=int, default=4,
                    help="concurrent MJPEG clients for the multi-client test")
    ap.add_argument("--with-xclk", action="store_true",
                    help="include the XCLK cycle test (RF-disruptive)")
    ap.add_argument("--xclk-restore", type=float, default=14.0,
                    help="XCLK (MHz) to restore after the xclk test")
    ap.add_argument("--only", help="comma-separated test names to run")
    args = ap.parse_args()

    print(f"CamCar functional tests -> {args.host}\n")
    suite = Suite()
    tests = make_tests(args.host, args)
    if args.only:
        want = set(args.only.split(","))
        tests = [t for t in tests if t[0] in want]
    for name, fn in tests:
        suite.run(name, fn)
    sys.exit(0 if suite.summary() else 1)


if __name__ == "__main__":
    main()

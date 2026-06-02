// A WebSocket that auto-reconnects: exponential backoff with jitter, clean
// teardown of the old socket, and backoff reset on a good open. Expose send(),
// readyState, connect() and reconnectNow().
function ReconnectingSocket(url, handlers) {
    var self = this;
    var ws = null, backoff = 1000, maxBackoff = 15000, timer = null;

    function cleanup() {
        if (ws) {
            ws.onopen = ws.onmessage = ws.onclose = ws.onerror = null;
            try { ws.close(); } catch (e) {}
            ws = null;
        }
    }
    function schedule() {
        if (timer) return;
        var delay = Math.min(backoff, maxBackoff) * (0.7 + 0.6 * Math.random());
        backoff = Math.min(backoff * 2, maxBackoff);
        timer = setTimeout(function () { timer = null; self.connect(); }, delay);
    }
    self.connect = function () {
        if (timer) { clearTimeout(timer); timer = null; }
        cleanup();
        ws = new WebSocket(url);
        ws.binaryType = 'blob';
        ws.onopen = function (e) { backoff = 1000; if (handlers.onopen) handlers.onopen(e); };
        ws.onmessage = handlers.onmessage || null;
        ws.onerror = function () { try { ws.close(); } catch (e) {} };  // -> onclose
        ws.onclose = function (e) { if (handlers.onclose) handlers.onclose(e); schedule(); };
    };
    self.send = function (d) {
        if (ws && ws.readyState === WebSocket.OPEN) { ws.send(d); return true; }
        return false;
    };
    self.reconnectNow = function () { backoff = 1000; self.connect(); };
    Object.defineProperty(self, 'readyState', {
        get: function () { return ws ? ws.readyState : WebSocket.CLOSED; }
    });
}

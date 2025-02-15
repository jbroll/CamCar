function initCameraWebSocket(url) {
    var websocketCamera = new WebSocket(webSocketCameraUrl);
    websocketCamera.binaryType = 'blob';
    websocketCamera.onopen    = function(event){
      document.getElementById('connectionStatus').textContent = 'Camera Connected';
    };
    websocketCamera.onclose   = function(event){
      document.getElementById('connectionStatus').textContent = 'Camera Disconnected - Retrying...';
      setTimeout(initCameraWebSocket, 2000);
    };
    websocketCamera.onmessage = function(event) {
      var imageId = document.getElementById("cameraImage");
      imageId.src = URL.createObjectURL(event.data);
    };

    return websocketCamera;
}

window.initCameraWebSocket = initCameraWebSocket;


  function initCarInputWebSocket() {
    var websocketCarInput = new WebSocket(webSocketCarInputUrl);
    websocketCarInput.onopen    = function(event) {
      document.getElementById('connectionStatus').textContent = 'Controls Connected';
      // Initialize slider values after connection established
      sendButtonInput("Speed", document.getElementById("Speed").value);
      sendButtonInput("Light", document.getElementById("Light").value);
      sendButtonInput("Pan", document.getElementById("Pan").value);
      sendButtonInput("Tilt", document.getElementById("Tilt").value);                    
    };
    websocketCarInput.onclose   = function(event){
      document.getElementById('connectionStatus').textContent = 'Controls Disconnected - Retrying...';
      setTimeout(initCarInputWebSocket, 2000);
    };
    websocketCarInput.onmessage = function(event){};        

    return websocketCarInput;
  }

window.initCarInputWebSocket = initCarInputWebSocket;
      

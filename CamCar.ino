#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <ESP32Servo.h>

#include "./Prefs.h"
#include "src/gen/secrets.h"
#include "src/board_config.h"

#include "src/Camera.h"
#include "src/MjpegStreamServer.h"
#include "src/RtspStreamServer.h"

#include "src/PrefEdit.h"
#include "src/WebHandler.h"
#include "src/TankDrive.h"

Servo panServo;
Servo tiltServo;

const int PWMFreq = 1000; /* 1 KHz */
const int PWMResolution = 8;

// Battery sense (only where BATTERY_PIN >= 0; S3 only). Wire battery+ through a
// resistor divider into BATTERY_PIN. ADJUST THESE to your divider and pack:
//   BATTERY_DIVIDER = (R1 + R2) / R2   (e.g. 200k/100k -> Vbat = Vadc * 3.0)
//   keep Vadc under ~3.0V at full charge.
const float BATTERY_DIVIDER = 3.0f;   // 200k(top)/100k(bottom)
const float BATTERY_VMIN    = 6.0f;   // 0%  (2S LiPo empty)
const float BATTERY_VMAX    = 8.4f;   // 100% (2S LiPo full)

AsyncWebServer server(80);
AsyncWebSocket wsCamera("/Camera");
AsyncWebSocket wsCarInput("/CarInput");
uint32_t cameraClientId = 0;

CameraHandler camera(wsCamera);

// Streaming servers, each on its own port + FreeRTOS task, consuming the single
// producer's frames (see StreamServer.h). HTTP-MJPEG on :81, RTSP on :554.
MjpegStreamServer mjpegServer(camera, 81);
RtspStreamServer  rtspServer(camera, 554);

// Sign-magnitude drive for one motor: speed -100..100 via PWM on its two input
// pins (the H-bridge enable is tied high). PWM the input matching the direction
// and hold the other low -- independent proportional speed, no separate enable.
void driveMotor(int in1Pin, int in2Pin, int speed) {
  speed = constrain(speed, -100, 100);
  int duty = map(abs(speed), 0, 100, 0, 255);
  if (speed > 0) {
    ledcWrite(in1Pin, duty);
    ledcWrite(in2Pin, 0);
  } else if (speed < 0) {
    ledcWrite(in1Pin, 0);
    ledcWrite(in2Pin, duty);
  } else {
    ledcWrite(in1Pin, 0);
    ledcWrite(in2Pin, 0);
  }
}

// Arcade/tank mix: y = throttle, x = turn (both -100..100). Proportional
// differential steering -- e.g. forward + slight turn curves gently.
void tankDrive(int x, int y) {
  int left  = constrain(y + x, -100, 100);
  int right = constrain(y - x, -100, 100);
  driveMotor(RIGHT_MOTOR_IN1, RIGHT_MOTOR_IN2, right);
  driveMotor(LEFT_MOTOR_IN1,  LEFT_MOTOR_IN2,  left);
}

// Camera pan/tilt: x/y in -100..100 -> servo angle 0..180 (90 = centre).
void cameraControl(int x, int y) {
  panServo.write(map(x, -100, 100, 0, 180));
  tiltServo.write(map(y, -100, 100, 0, 180));
}

void onCarInputWebSocketEvent(AsyncWebSocket *server, 
                      AsyncWebSocketClient *client, 
                      AwsEventType type,
                      void *arg, 
                      uint8_t *data, 
                      size_t len) {                      
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      tankDrive(0, 0);          // safe stop
      panServo.write(90);
      tiltServo.write(90);
      break;
    case WS_EVT_DATA:
      AwsFrameInfo *info;
      info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 &&
          info->len == len && info->opcode == WS_TEXT) {
        std::string myData((char *)data, len);
        std::istringstream ss(myData);
        std::string cmd;
        ss >> cmd;   // first whitespace-delimited token
        if (cmd == "tank") {            // "tank <x> <y>" drive joystick
          int x = 0, y = 0;
          ss >> x >> y;
          tankDrive(x, y);
        } else if (cmd == "camr") {     // "camr <x> <y>" pan/tilt joystick
          int x = 0, y = 0;
          ss >> x >> y;
          cameraControl(x, y);
        } else {                        // "key,value" commands (e.g. Resolution,N)
          std::istringstream cs(myData);
          std::string key, value;
          std::getline(cs, key, ',');
          std::getline(cs, value, ',');
          int valueInt = atoi(value.c_str());
          if (key == "Resolution") {
            camera.setResolution((uint8_t)valueInt);  // ladder index (0..N-1)
          } else if (key == "Quality") {
            camera.setQuality((uint8_t)valueInt);      // esp_camera q (4..63)
          } else if (key == "Xclk") {
            // Xclk,<MHz> -- runtime XCLK, fractional OK (2..20). See XCLK lesson.
            // Persisted to NVS so it survives reboot (applied after WiFi joins).
            float mhz = atof(value.c_str());
            if (mhz < 2.0f)  mhz = 2.0f;
            if (mhz > 20.0f) mhz = 20.0f;
            camera.setXclkFreq((uint32_t)(mhz * 1000000.0f));
            char xbuf[8];
            snprintf(xbuf, sizeof(xbuf), "%.1f", mhz);
            PrefEdit::set("xclk", xbuf);
          } else if (key == "XclkScan") {
            camera.startScan();   // auto-tune; winner persisted in loop()
          } else if (key == "Fps") {
            // Max-fps cap. Pace the grab to a link-sustainable rate -> smooth
            // delivery instead of race/stall on a marginal link. Persisted.
            camera.setFPS((uint8_t)atoi(value.c_str()));
            char fbuf[8];
            snprintf(fbuf, sizeof(fbuf), "%u", camera.getFPS());
            PrefEdit::set("fps", fbuf);
          } else if (key == "Camera") {
            // Camera,0 -> stop (deinit, XCLK off, clears WiFi); Camera,1 -> start
            camera.setCameraEnabled(valueInt != 0);
          } else if (key == "Lock") {
            // Lock,1 -> freeze resolution (disable auto-adapt); Lock,0 -> resume
            camera.setAdaptEnabled(valueInt == 0);
          } else if (key == "Light") {
            // Light,1 -> headlight on; Light,0 -> off
            if (LIGHT_PIN >= 0) digitalWrite(LIGHT_PIN, valueInt > 0 ? HIGH : LOW);
          }
        }
      }
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
    default:
      break;  
  }
}

void onCameraWebSocketEvent(AsyncWebSocket *server,
                          AsyncWebSocketClient *client,
                          AwsEventType type,
                          void *arg,
                          uint8_t *data,
                          size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n",
                         client->id(), client->remoteIP().toString().c_str());
            camera.setClientId(client->id());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            camera.clearClientId();
            break;
        case WS_EVT_DATA:
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}

void setUpPinModes() {
  panServo.attach(PAN_PIN);
  tiltServo.attach(TILT_PIN);

  // The H-bridge enable is tied HIGH to 3.3V in hardware (always enabled), so
  // the firmware leaves it alone -- GPIO 1 (MOTOR_SPEED_PIN) is free. Speed
  // comes from PWM on the direction inputs (sign-magnitude).

  // PWM the four direction inputs on explicit LEDC channels 2-5 (avoiding the
  // camera's channel 0). Two channels per motor give independent proportional
  // speed without separate enable pins.
  ledcAttachChannel(RIGHT_MOTOR_IN1, PWMFreq, PWMResolution, 2);
  ledcAttachChannel(RIGHT_MOTOR_IN2, PWMFreq, PWMResolution, 3);
  ledcAttachChannel(LEFT_MOTOR_IN1,  PWMFreq, PWMResolution, 4);
  ledcAttachChannel(LEFT_MOTOR_IN2,  PWMFreq, PWMResolution, 5);
  tankDrive(0, 0);

  if (LIGHT_PIN >= 0) {        // some boards have no spare pin for a light
    pinMode(LIGHT_PIN, OUTPUT);
    digitalWrite(LIGHT_PIN, LOW);
  }
}

// WiFi connection behaviour
#define WIFI_CONNECT_TIMEOUT_MS 15000  // give up on a stored network after this
#define WIFI_CONNECT_BLINK_MS   500    // status LED toggle / poll interval
const char* SETUP_AP_SSID = "CamCar-setup";
const char* SETUP_AP_PASSWORD = "camcarsetup";  // must be >= 8 chars (WPA2)

// Bring up a SoftAP so credentials can be entered at
// http://192.168.4.1/config when there is no usable station network.
void startSetupAP() {
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(SETUP_AP_SSID, SETUP_AP_PASSWORD)) {
    Serial.println("Failed to start setup access point!");
    return;
  }
  Serial.printf("Setup AP started. SSID: %s  Password: %s\n",
                SETUP_AP_SSID, SETUP_AP_PASSWORD);
  Serial.print("Open http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/config to set WiFi credentials.");
}

// Read WiFi credentials from NVS (seeded from build-time .env defaults on
// first boot) and join as a station. With no stored SSID, or if the join
// times out, fall back to the setup SoftAP instead of blocking forever.
// Network name: the "hostname" pref if set, else "camcar-<low 3 MAC bytes>" so
// multiple cars on one LAN don't collide (e.g. camcar-3c71bf).
String networkHostname() {
  String h = PrefEdit::get("hostname");
  if (h.length() > 0) return h;
  uint64_t mac = ESP.getEfuseMac();
  char buf[20];
  snprintf(buf, sizeof(buf), "camcar-%02x%02x%02x",
           (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16));
  return String(buf);
}

void setupWiFi() {
  PrefEdit::initStorage();

  // First boot: seed NVS from compile-time defaults if .env provided any.
  if (PrefEdit::get("ssid").length() == 0 && strlen(WIFI_SSID_DEFAULT) > 0) {
    PrefEdit::set("ssid", WIFI_SSID_DEFAULT);
    PrefEdit::set("password", WIFI_PASSWORD_DEFAULT);
    Serial.println("Seeded WiFi credentials from build-time defaults.");
  }

  String ssid = PrefEdit::get("ssid");
  String password = PrefEdit::get("password");

  if (ssid.length() == 0) {
    Serial.println("No WiFi credentials stored; starting setup AP.");
    startSetupAP();
    return;
  }

  String host = networkHostname();
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());   // DHCP option 12 -> router shows the name
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to WiFi network '%s' as '%s' ", ssid.c_str(), host.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // Toggle LED
    Serial.print(".");
    delay(WIFI_CONNECT_BLINK_MS);
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Disable modem-sleep AFTER association so it reliably sticks; this is
    // critical for low, stable latency (power-save causes ~400ms RTT spikes).
    WiFi.setSleep(false);
    Serial.print("\nConnected to the WiFi network at ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %ld dBm\n", (long)WiFi.RSSI());
    if (MDNS.begin(host.c_str())) {                 // -> http://<host>.local/
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS: http://%s.local/\n", host.c_str());
    }
  } else {
    Serial.println("\nWiFi connection timed out; starting setup AP.");
    startSetupAP();
  }
}

void setup(void) {
  setUpPinModes();
  Serial.begin(115200);
  delay(1000); 
  Serial.printf("BEGIN\n");

  pinMode(STATUS_LED, OUTPUT);
  //pinMode(FLASH_LED, OUTPUT);

  Serial.println("\n\nESP32-CAM Test Starting...");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Flash Size: %d bytes\n", ESP.getFlashChipSize());

  setupWiFi();

  PrefEdit::begin(&server, "/config", configParams);

  // High-res still: GET /snapshot?res=<index>[&download=1]. Pauses the stream,
  // captures one frame at the requested size, then resumes.
  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* request) {
    uint8_t idx = 0;
    if (request->hasParam("res")) {
      idx = (uint8_t)request->getParam("res")->value().toInt();
    }
    bool download = request->hasParam("download");
    camera_fb_t* fb = camera.captureSnapshot(idx);
    if (!fb) {
      request->send(503, "text/plain", "snapshot capture failed");
      return;
    }
    size_t len = fb->len;
    AsyncWebServerResponse* response = request->beginResponse("image/jpeg", len,
      [fb, len](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
        size_t remaining = len - index;
        size_t n = remaining < maxLen ? remaining : maxLen;
        memcpy(buffer, fb->buf + index, n);
        if (index + n >= len) {
          esp_camera_fb_return(fb);  // free after the final chunk is sent
        }
        return n;
      });
    response->addHeader("Cache-Control", "no-store");
    if (download) {
      response->addHeader("Content-Disposition", "attachment; filename=snapshot.jpg");
    }
    request->send(response);
  });

  // Live MJPEG for VLC/ffmpeg/NVRs is served by mjpegServer on :81 (its own
  // WiFiServer + task; see MjpegStreamServer.h). It consumes the single
  // producer's frames via copyLatestFrame(), so it never grabs the camera and
  // never blocks the AsyncTCP event loop. Started in setup() after WiFi is up.

  WebHandler::begin(server);

  // Pacing cap is just a high ceiling -- the camera clock (XCLK, via the
  // blocking grab) is the real pacer, so fps follows the selected XCLK.
  camera.setFPS(CameraHandler::MAX_FPS);
  wsCamera.onEvent(onCameraWebSocketEvent);
  server.addHandler(&wsCamera);

  wsCarInput.onEvent(onCarInputWebSocketEvent);
  server.addHandler(&wsCarInput);

  server.begin();
  Serial.println("HTTP server started");

  if (!camera.begin()) {
    Serial.println("Camera initialization failed!");
    return;
  }

  // Start the streaming servers now that WiFi + camera are up. Each runs on its
  // own task and consumes the producer's frames; neither grabs the camera.
  mjpegServer.begin();
  Serial.println("MJPEG stream server started on :81");
  rtspServer.begin();
  Serial.println("RTSP stream server started on :554 (rtsp://<host>:554/mjpeg/1)");

  // Apply a persisted XCLK (set via the UI menu). Done here -- after WiFi has
  // associated and the camera is up -- so a bad saved value can't harm the join;
  // the camera-stop button is the recovery if it disturbs streaming.
  String savedXclk = PrefEdit::get("xclk");
  if (savedXclk.length() > 0) {
    float mhz = atof(savedXclk.c_str());
    if (mhz >= 2.0f && mhz <= 20.0f) {
      camera.setXclkFreq((uint32_t)(mhz * 1000000.0f));
      Serial.printf("Applied saved XCLK: %s MHz\n", savedXclk.c_str());
    }
  }
  String savedFps = PrefEdit::get("fps");   // persisted max-fps cap
  if (savedFps.length() > 0) {
    int n = atoi(savedFps.c_str());
    if (n >= 1 && n <= 30) camera.setFPS((uint8_t)n);
  }
}

void loop() {
  //digitalWrite(STATUS_LED, HIGH);
  //digitalWrite(FLASH_LED, LOW);
  //delay(1000);

  //digitalWrite(STATUS_LED, LOW);
  //digitalWrite(FLASH_LED, HIGH);
  //delay(1000);

  PrefEdit::loop();

  wsCamera.cleanupClients();
  wsCarInput.cleanupClients();

  camera.scanTick();                       // advance an auto-tune scan, if running
  if (camera.consumeScanDone()) {          // persist the scan's winner
    char xbuf[8];
    snprintf(xbuf, sizeof(xbuf), "%.1f", camera.getXclkFreq() / 1000000.0f);
    PrefEdit::set("xclk", xbuf);
    Serial.printf("[scan] persisted XCLK %s MHz\n", xbuf);
  }

#if BATTERY_PIN >= 0
  // Push battery voltage/percent to the page every 2s (text frame "bat <V> <%>",
  // alongside the camera socket's uptime frame).
  static uint32_t lastBattery = 0;
  if (millis() - lastBattery > 2000) {
    lastBattery = millis();
    if (wsCamera.count() > 0) {
      uint32_t mv = 0;
      for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(BATTERY_PIN);
      float vbat = (mv / 8.0f / 1000.0f) * BATTERY_DIVIDER;
      int pct = (int)((vbat - BATTERY_VMIN) / (BATTERY_VMAX - BATTERY_VMIN) * 100.0f);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      char buf[32];
      snprintf(buf, sizeof(buf), "bat %.2f %d", vbat, pct);
      wsCamera.textAll(buf);
    }
  }
#endif

  if ( !camera.sendFrame() ) {
      delay(1);
  }
}

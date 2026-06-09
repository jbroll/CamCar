#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <ESP32Servo.h>

#include "src/Prefs.h"
#include "src/gen/secrets.h"
#include "src/board_config.h"

#include "src/Camera.h"
#include "src/MjpegStreamServer.h"
#include "src/RtspStreamServer.h"

#include "src/PrefEdit.h"
#include "src/WebHandler.h"
#include "src/OtaWeb.h"

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
// ---- Drive/camera calibration (NVS-backed; see Prefs.h). Defaults = no-op. ----
struct DriveCal {
  bool motSwap = false, motInvL = false, motInvR = false;
  int  lMin = 0, lMax = 255, rMin = 0, rMax = 255;     // per-motor PWM floor/cap
  bool camSwap = false, panInv = false, tiltInv = false;
  int  panMin = 0, panMax = 180, tiltMin = 0, tiltMax = 180;  // servo travel (deg)
} gCal;

static bool prefBool(const char* k) { return PrefEdit::get(k, "0") == "1"; }
static int  prefInt(const char* k, int def, int lo, int hi) {
  return constrain((int)PrefEdit::get(k, String(def)).toInt(), lo, hi);
}

// Reload calibration from NVS into gCal -- called at boot and on each /config save.
void loadDriveCal() {
  PrefEdit::initStorage();   // idempotent; safe regardless of call order
  gCal.motSwap = prefBool("mot_swap");
  gCal.motInvL = prefBool("mot_inv_l");
  gCal.motInvR = prefBool("mot_inv_r");
  gCal.lMin = prefInt("mot_l_min",   0, 0, 255);
  gCal.lMax = prefInt("mot_l_max", 255, 0, 255);
  gCal.rMin = prefInt("mot_r_min",   0, 0, 255);
  gCal.rMax = prefInt("mot_r_max", 255, 0, 255);
  gCal.camSwap = prefBool("cam_swap");
  gCal.panInv  = prefBool("pan_inv");
  gCal.tiltInv = prefBool("tilt_inv");
  gCal.panMin  = prefInt("pan_min",    0, 0, 180);
  gCal.panMax  = prefInt("pan_max",  180, 0, 180);
  gCal.tiltMin = prefInt("tilt_min",   0, 0, 180);
  gCal.tiltMax = prefInt("tilt_max", 180, 0, 180);
}

// Sign-magnitude PWM on the active input. Duty scales abs(speed) into the
// motor's [pwmMin,pwmMax]: pwmMin overcomes stiction, pwmMax caps top speed.
void driveMotor(int in1Pin, int in2Pin, int speed, int pwmMin, int pwmMax) {
  speed = constrain(speed, -100, 100);
  int duty = (speed == 0) ? 0 : constrain(map(abs(speed), 0, 100, pwmMin, pwmMax), 0, 255);
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
// differential steering, then calibration: swap routes the mix to the other
// physical motor; invert reverses each physical motor's direction.
void tankDrive(int x, int y) {
  int left  = constrain(y + x, -100, 100);
  int right = constrain(y - x, -100, 100);
  if (gCal.motSwap) { int t = left; left = right; right = t; }
  if (gCal.motInvL) left  = -left;
  if (gCal.motInvR) right = -right;
  driveMotor(LEFT_MOTOR_IN1,  LEFT_MOTOR_IN2,  left,  gCal.lMin, gCal.lMax);
  driveMotor(RIGHT_MOTOR_IN1, RIGHT_MOTOR_IN2, right, gCal.rMin, gCal.rMax);
}

// Camera pan/tilt: x/y in -100..100, calibrated -- swap axes, invert each, then
// map into the per-servo travel limits (default 0..180, 90 = centre).
void cameraControl(int x, int y) {
  if (gCal.camSwap) { int t = x; x = y; y = t; }
  if (gCal.panInv)  x = -x;
  if (gCal.tiltInv) y = -y;
  panServo.write (map(x, -100, 100, gCal.panMin,  gCal.panMax));
  tiltServo.write(map(y, -100, 100, gCal.tiltMin, gCal.tiltMax));
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

// WiFi connection behaviour. A marginal/detuned antenna (e.g. header pins near
// the module) often misses the *first* association but holds the link fine once
// up, so we re-issue WiFi.begin() in cycles instead of giving up after one
// window, and reconnect on drops rather than stranding the board offline.
#define WIFI_CONNECT_TIMEOUT_MS 45000  // total boot window before falling back to SoftAP
#define WIFI_RETRY_INTERVAL_MS  7000   // re-issue WiFi.begin() this often while connecting
#define WIFI_CONNECT_BLINK_MS   250    // status LED toggle / poll interval
#define WIFI_RECONNECT_CHECK_MS 5000   // loop() reconnect-watchdog cadence
// TX power: lowered from max (19.5dBm). High TX draws current spikes that brown
// out a marginal supply and can self-interfere during the 4-way handshake -- a
// documented "reason 2 / auth expired" fix is ~8.5dBm. Bump back up if range suffers.
#define WIFI_TX_POWER           WIFI_POWER_8_5dBm

static bool gWifiStaActive = false;    // true once associated as STA (not SoftAP fallback)
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
  WiFi.persistent(false);           // creds live in NVS via PrefEdit; don't thrash WiFi flash
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(host.c_str());   // DHCP option 12 -> router shows the name
  WiFi.setAutoReconnect(true);      // IDF re-associates on drop (loop() backstops it)
  WiFi.setSleep(false);             // no modem-sleep: low, stable latency
  WiFi.setTxPower(WIFI_TX_POWER);   // max TX to push a marginal/detuned antenna over the line

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to WiFi network '%s' as '%s' ", ssid.c_str(), host.c_str());

  // Re-issue begin() in cycles: a marginal antenna can miss several attempts
  // before catching, so retry instead of dropping to SoftAP after one window.
  unsigned long start = millis();
  unsigned long lastBegin = start;
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    if (millis() - lastBegin >= WIFI_RETRY_INTERVAL_MS) {
      Serial.print("[retry]");
      WiFi.disconnect();
      WiFi.begin(ssid.c_str(), password.c_str());
      lastBegin = millis();
    }
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // Toggle LED
    Serial.print(".");
    delay(WIFI_CONNECT_BLINK_MS);
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Re-assert after association so they reliably stick (power-save causes
    // ~400ms RTT spikes; TX power can reset on (re)assoc on some cores).
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_TX_POWER);
    gWifiStaActive = true;           // arm the loop() reconnect watchdog
    Serial.print("\nConnected to the WiFi network at ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %ld dBm  TX: %d (0.25dBm units)\n",
                  (long)WiFi.RSSI(), (int)WiFi.getTxPower());
    if (MDNS.begin(host.c_str())) {                 // -> http://<host>.local/
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mDNS: http://%s.local/\n", host.c_str());
    }
  } else {
    Serial.println("\nWiFi connection timed out; starting setup AP.");
    startSetupAP();
  }
}

// loop() backstop: if we associated as STA and the link later drops, re-issue
// begin() periodically (belt-and-suspenders alongside setAutoReconnect). Does
// nothing in SoftAP-fallback mode (gWifiStaActive stays false).
void wifiReconnectTick() {
  if (!gWifiStaActive) return;
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < WIFI_RECONNECT_CHECK_MS) return;
  lastCheck = millis();
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.println("[wifi] link down -- reconnecting");
  WiFi.disconnect();
  WiFi.begin(PrefEdit::get("ssid").c_str(), PrefEdit::get("password").c_str());
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
  loadDriveCal();                  // drive/camera calibration from NVS

  if (PrefEdit::get("device_pass").length() == 0)
    PrefEdit::set("device_pass", DEVICE_PASSWORD_DEFAULT);
  OtaWeb::begin(&server, &camera);

  // Login: a correct device password sets the camcar_auth cookie that gates the
  // whole port-80 app (UI, websockets, config, OTA, snapshot). The :81 MJPEG and
  // :554 RTSP servers gate the same password via HTTP/RTSP Basic auth (blank or
  // "camcar" username), so NVR/Motion clients authenticate with it too.
  server.on("/login", HTTP_POST, [](AsyncWebServerRequest* request) {
    String pass = request->hasParam("password", true)
                  ? request->getParam("password", true)->value() : String();
    bool ok = pass.length() && pass == PrefEdit::get("device_pass", "camcar");
    AsyncWebServerResponse* resp = request->beginResponse(303);
    resp->addHeader("Location", ok ? "/" : "/?e=1");
    if (ok) {
      resp->addHeader("Set-Cookie",
        "camcar_auth=" + pass + "; Path=/; Max-Age=31536000; HttpOnly; SameSite=Lax");
    }
    request->send(resp);
  });

  server.on("/config.json", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!PrefEdit::checkAuth(request)) { request->send(401); return; }
    String json = "{";
    json += "\"hostname\":\"" + PrefEdit::get("hostname") + "\",";
    json += "\"hostname_effective\":\"" + networkHostname() + "\",";
    json += "\"ssid\":\"" + PrefEdit::get("ssid") + "\",";
    // Calibration (from gCal so values reflect the live, defaulted state):
    json += "\"mot_swap\":"  + String(gCal.motSwap ? 1 : 0) + ",";
    json += "\"mot_inv_l\":" + String(gCal.motInvL ? 1 : 0) + ",";
    json += "\"mot_inv_r\":" + String(gCal.motInvR ? 1 : 0) + ",";
    json += "\"mot_l_min\":" + String(gCal.lMin) + ",";
    json += "\"mot_l_max\":" + String(gCal.lMax) + ",";
    json += "\"mot_r_min\":" + String(gCal.rMin) + ",";
    json += "\"mot_r_max\":" + String(gCal.rMax) + ",";
    json += "\"cam_swap\":"  + String(gCal.camSwap ? 1 : 0) + ",";
    json += "\"pan_inv\":"   + String(gCal.panInv ? 1 : 0) + ",";
    json += "\"tilt_inv\":"  + String(gCal.tiltInv ? 1 : 0) + ",";
    json += "\"pan_min\":"   + String(gCal.panMin) + ",";
    json += "\"pan_max\":"   + String(gCal.panMax) + ",";
    json += "\"tilt_min\":"  + String(gCal.tiltMin) + ",";
    json += "\"tilt_max\":"  + String(gCal.tiltMax);
    json += "}";
    AsyncWebServerResponse* resp = request->beginResponse(200, "application/json", json);
    resp->addHeader("Cache-Control", "no-store");
    request->send(resp);
  });

  // High-res still: GET /snapshot?res=<index>[&download=1]. Pauses the stream,
  // captures one frame at the requested size, then resumes.
  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!PrefEdit::checkAuth(request)) { request->send(401); return; }
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
  AwsHandshakeHandler gate = [](AsyncWebServerRequest* r) { return PrefEdit::checkAuth(r); };
  wsCamera.onEvent(onCameraWebSocketEvent);
  wsCamera.handleHandshake(gate);
  server.addHandler(&wsCamera);

  wsCarInput.onEvent(onCarInputWebSocketEvent);
  wsCarInput.handleHandshake(gate);
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
  if (PrefEdit::consumeChanged()) loadDriveCal();   // apply calibration edits live
  OtaWeb::loop();
  wifiReconnectTick();             // re-associate if the link dropped

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

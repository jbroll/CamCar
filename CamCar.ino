#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <ESP32Servo.h>

#include "./Prefs.h"
#include "src/gen/secrets.h"
#include "src/board_config.h"

#include "src/Camera.h"

#include "src/PrefEdit.h"
#include "src/WebHandler.h"
#include "src/TankDrive.h"

Servo panServo;
Servo tiltServo;

struct MOTOR_PINS {
  int pinEn;  
  int pinIN1;
  int pinIN2;    
};

std::vector<MOTOR_PINS> motorPins = {
  {MOTOR_SPEED_PIN, RIGHT_MOTOR_IN1, RIGHT_MOTOR_IN2}, //RIGHT_MOTOR (EnA, IN1, IN2)
  {MOTOR_SPEED_PIN, LEFT_MOTOR_IN1,  LEFT_MOTOR_IN2},  //LEFT_MOTOR  (EnB, IN3, IN4)
};

#define UP 1
#define DOWN 2
#define LEFT 3
#define RIGHT 4
#define STOP 0

#define RIGHT_MOTOR 0
#define LEFT_MOTOR 1

#define FORWARD 1
#define BACKWARD -1

const int PWMFreq = 1000; /* 1 KHz */
const int PWMResolution = 8;
const int PWMSpeedChannel = 2;
const int PWMLightChannel = 3;

AsyncWebServer server(80);
AsyncWebSocket wsCamera("/Camera");
AsyncWebSocket wsCarInput("/CarInput");
uint32_t cameraClientId = 0;

CameraHandler camera(wsCamera);

void rotateMotor(int motorNumber, int motorDirection) {
  if (motorDirection == FORWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, HIGH);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);    
  } else if (motorDirection == BACKWARD) {
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, HIGH);     
  } else {
    digitalWrite(motorPins[motorNumber].pinIN1, LOW);
    digitalWrite(motorPins[motorNumber].pinIN2, LOW);       
  }
}

void moveCar(int inputValue) {
  Serial.printf("Got value as %d\n", inputValue);  
  switch(inputValue) {
    case UP:
      rotateMotor(RIGHT_MOTOR, FORWARD);
      rotateMotor(LEFT_MOTOR, FORWARD);                  
      break;
  
    case DOWN:
      rotateMotor(RIGHT_MOTOR, BACKWARD);
      rotateMotor(LEFT_MOTOR, BACKWARD);  
      break;
  
    case LEFT:
      rotateMotor(RIGHT_MOTOR, FORWARD);
      rotateMotor(LEFT_MOTOR, BACKWARD);  
      break;
  
    case RIGHT:
      rotateMotor(RIGHT_MOTOR, BACKWARD);
      rotateMotor(LEFT_MOTOR, FORWARD); 
      break;
 
    case STOP:
      rotateMotor(RIGHT_MOTOR, STOP);
      rotateMotor(LEFT_MOTOR, STOP);    
      break;
  
    default:
      rotateMotor(RIGHT_MOTOR, STOP);
      rotateMotor(LEFT_MOTOR, STOP);    
      break;
  }
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
      moveCar(0);
      ledcWrite(LIGHT_PIN, 0);
      panServo.write(90);
      tiltServo.write(90);       
      break;
    case WS_EVT_DATA:
      AwsFrameInfo *info;
      info = (AwsFrameInfo*)arg;
      if (info->final && info->index == 0 && 
          info->len == len && info->opcode == WS_TEXT) {
        std::string myData = "";
        myData.assign((char *)data, len);
        std::istringstream ss(myData);
        std::string key, value;
        std::getline(ss, key, ',');
        std::getline(ss, value, ',');
        Serial.printf("Key [%s] Value[%s]\n", key.c_str(), value.c_str()); 
        int valueInt = atoi(value.c_str());     
        if (key == "MoveCar") {
          moveCar(valueInt);        
        } else if (key == "Speed") {
          ledcWrite(motorPins[RIGHT_MOTOR].pinEn, valueInt);
        } else if (key == "Light") {
          ledcWrite(LIGHT_PIN, valueInt);
        } else if (key == "Pan") {
          panServo.write(valueInt);
        } else if (key == "Tilt") {
          tiltServo.write(valueInt);
        } else if (key == "Resolution") {
          camera.setResolution((uint8_t)valueInt);  // value is a ladder index (0..N-1)
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

  for (int i = 0; i < motorPins.size(); i++) {
    pinMode(motorPins[i].pinEn, OUTPUT);
    pinMode(motorPins[i].pinIN1, OUTPUT);
    pinMode(motorPins[i].pinIN2, OUTPUT);
  }

  // Both motor enable pins are wired to the same GPIO, so a single PWM
  // channel drives the shared speed signal. (esp32 core 3.x ledc API is
  // pin-based; ledcAttachChannel pins an explicit channel to avoid the
  // camera's LEDC channel 0.)
  ledcAttachChannel(motorPins[RIGHT_MOTOR].pinEn, PWMFreq, PWMResolution, PWMSpeedChannel);
  moveCar(STOP);

  pinMode(LIGHT_PIN, OUTPUT);
  ledcAttachChannel(LIGHT_PIN, PWMFreq, PWMResolution, PWMLightChannel);
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

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.printf("Connecting to WiFi network '%s' ", ssid.c_str());

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
  WebHandler::begin(server);

  camera.setFPS(10);
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

  if ( !camera.sendFrame() ) {
      delay(1); 
  }
}

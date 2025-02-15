#include "esp_camera.h"
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <iostream>
#include <sstream>
#include <ESP32Servo.h>

#include "./Prefs.h"

#include "src/Camera.h"

#include "src/PrefEdit.h"
#include "src/WebHandler.h"
#include "src/TankDrive.h"

#define PAN_PIN 14
#define TILT_PIN 15

Servo panServo;
Servo tiltServo;

struct MOTOR_PINS {
  int pinEn;  
  int pinIN1;
  int pinIN2;    
};

std::vector<MOTOR_PINS> motorPins = {
  {2, 12, 13}, //RIGHT_MOTOR Pins (EnA, IN1, IN2)
  {2, 1, 3},  //LEFT_MOTOR  Pins (EnB, IN3, IN4)
};
#define LIGHT_PIN 4

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
      ledcWrite(PWMLightChannel, 0); 
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
          ledcWrite(PWMSpeedChannel, valueInt);
        } else if (key == "Light") {
          ledcWrite(PWMLightChannel, valueInt);         
        } else if (key == "Pan") {
          panServo.write(valueInt);
        } else if (key == "Tilt") {
          tiltServo.write(valueInt);   
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

  //Set up PWM
  ledcSetup(PWMSpeedChannel, PWMFreq, PWMResolution);
  ledcSetup(PWMLightChannel, PWMFreq, PWMResolution);
      
  for (int i = 0; i < motorPins.size(); i++) {
    pinMode(motorPins[i].pinEn, OUTPUT);    
    pinMode(motorPins[i].pinIN1, OUTPUT);
    pinMode(motorPins[i].pinIN2, OUTPUT);  
    /* Attach the PWM Channel to the motor enb Pin */
    ledcAttachPin(motorPins[i].pinEn, PWMSpeedChannel);
  }
  moveCar(STOP);

  pinMode(LIGHT_PIN, OUTPUT);    
  ledcAttachPin(LIGHT_PIN, PWMLightChannel);
}

#define STATUS_LED 33  // Built-in status LED
#define FLASH_LED 4    // Flash/torch LED

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

  const boolean AP = false;
# define WIFI_CONNECT_DELAY 500

  if ( AP ) {
      const char* ssid = "CamCar";
      const char* password = "1234567890";

      if ( !WiFi.softAP(ssid, password)) {
          Serial.println("Failed to create WiFi access point!");
      }

      Serial.printf("AP SSID: %s\n", WiFi.softAPSSID().c_str());
      Serial.printf("AP Password: %s\n", password);
      Serial.printf("AP Channel: %d\n", WiFi.channel());
      Serial.printf("AP Max Connections: %d\n", WiFi.softAPgetStationNum());
      Serial.printf("AP IP address:");
      Serial.println(WiFi.softAPIP());
  } else {
      const char* ssid = "lucky7";
      const char* password = "snowblower";

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.println("\nConnecting to WiFi Network ..");

    while(WiFi.status() != WL_CONNECTED){
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));  // Toggle LED
      Serial.print(".");
      delay(WIFI_CONNECT_DELAY);
    }

    Serial.printf("\nConnected to the WiFi network at ");
    Serial.println(WiFi.localIP());
  }

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

  wsCamera.cleanupClients(); 
  wsCarInput.cleanupClients(); 

  if ( !camera.sendFrame() ) {
      delay(1); 
  }
}

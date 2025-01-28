# CamCar: ESP32-CAM Remote Control Car

A web-controlled car project using the ESP32-CAM module, featuring live video streaming, servo-controlled camera movement, and intuitive touch controls.

## Features

- Real-time video streaming from ESP32-CAM
- Touch-based directional controls (Forward, Backward, Left, Right)
- Adjustable speed control
- LED light control with brightness adjustment
- Pan and tilt camera controls via servo motors
- Mobile-friendly web interface
- WebSocket communication for responsive control
- Self-hosted WiFi access point

## Hardware Requirements

- ESP32-CAM module
- 2 DC motors with drive circuit (e.g., L298N motor driver)
- 2 Servo motors for camera pan/tilt
- LED light
- Power supply
- Chassis and wheels

## Pin Configuration

- **Motor Control**
  - Right Motor: EN=2, IN1=12, IN2=13
  - Left Motor: EN=2, IN1=1, IN2=3
- **Servo Control**
  - Pan Servo: Pin 14
  - Tilt Servo: Pin 15
- **LED Light**: Pin 4
- **Camera**: Standard ESP32-CAM pins

## Software Requirements

- Arduino IDE
- Required Libraries:
  - ESP32Servo
  - ESPAsyncWebServer
  - AsyncTCP
  - esp32-camera

## Installation

1. Install the Arduino CLI and required dependencies:
```bash
make install
```

2. Build the project:
```bash
make build
```

3. Upload to your ESP32-CAM:
```bash
make upload
```

4. Monitor serial output (optional):
```bash
make monitor
```

## Usage

1. Power on the CamCar
2. Connect to the "CamCar" WiFi network (Password: 12345678)
3. Open a web browser and navigate to the IP address shown in the serial monitor (typically 192.168.4.1)
4. Use the touch interface to control the car:
   - Arrow buttons for movement
   - Sliders for speed, light, and camera adjustment

## Development

The project consists of several key components:

- `CamCar.ino`: Main Arduino sketch
- `index.html`: Web interface
- `test_serv.py`: Test server for development
- `gzipper.py`: Utility to compress web interface
- `Makefile`: Build and deployment automation

To modify the web interface:
1. Edit `index.html`
2. Run `make build` to generate the compressed header file
3. Upload the new firmware

## Testing

A test server is provided for development:

```bash
python3 test_serv.py
```

This allows testing the web interface without uploading to the ESP32-CAM.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Credits

- Original work by Ujwal Nandanwar
- Modified by John B. Roll jr.

## Troubleshooting

1. If the camera fails to initialize:
   - Check camera module connections
   - Ensure adequate power supply

2. If motors don't respond:
   - Verify motor driver connections
   - Check PWM channel configuration

3. Connection issues:
   - Confirm WiFi network visibility
   - Check IP address in serial monitor
   - Ensure WebSocket connections are established

## Contributing

1. Fork the repository
2. Create your feature branch
3. Commit your changes
4. Push to the branch
5. Create a new Pull Request
# Makefile for CamCar project

PYTHON=python3
ARDUINO_CLI=arduino-cli
GZIP_SCRIPT=gzipper.py
VENV := venv
PYTHON_VENV := $(VENV)/bin/python
PIP := $(VENV)/bin/pip

BOARD=esp32:esp32:esp32cam
PORT=/dev/ttyUSB0
BAUD=115200

HTML_FILE=index.html
HEADER_FILE=webpage.h
INO_FILE=CamCar.ino

.PHONY: all clean build install upload monitor tester venv clean-venv

all: build

$(HEADER_FILE): $(HTML_FILE) $(GZIP_SCRIPT)
	./$(GZIP_SCRIPT) $< $@

install:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	$(ARDUINO_CLI) -cli lib install ESP32Servo
	$(ARDUINO_CLI) -cli lib install --git-url https://github.com/me-no-dev/ESPAsyncWebServer
	$(ARDUINO_CLI) -cli lib install --git-url https://github.com/me-no-dev/AsyncTCP

build: $(HEADER_FILE)
	$(ARDUINO_CLI) compile --fqbn $(BOARD) $(INO_FILE)

upload: build
	$(ARDUINO_CLI) upload --fqbn $(BOARD) --port $(PORT) $(INO_FILE)

monitor:
	$(ARDUINO_CLI) monitor --port $(PORT) --config baudrate=$(BAUD)

clean:
	rm -f $(HEADER_FILE)
	rm -rf build/
	rm -f *.bin
	rm -f *.elf

ports:
	$(ARDUINO_CLI) board list


tester: $(VENV)/bin/activate
	(. $(VENV)/bin/activate; ./tester.py)

$(VENV)/bin/activate: requirements.txt
	python3 -m venv $(VENV)
	$(PIP) install --upgrade pip
	$(PIP) install -r requirements.txt

venv: $(VENV)/bin/activate

clean-venv:
	rm -rf $(VENV)


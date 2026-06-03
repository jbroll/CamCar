# Makefile for CamCar project

PYTHON=python3
ARDUINO_CLI=arduino-cli
GZIP_SCRIPT=gzipper.py
VENV := venv
PYTHON_VENV := $(VENV)/bin/python
PIP := $(VENV)/bin/pip

# Board target: s3 (Freenove ESP32-S3-WROOM CAM, default) or cam (AI-Thinker
# ESP32-CAM). Override per-invocation, e.g. `make upload TARGET=cam`.
TARGET ?= s3
ifeq ($(TARGET),cam)
  BOARD = esp32:esp32:esp32cam
  PORT ?= /dev/ttyUSB0
else
  BOARD = esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app
  PORT ?= /dev/ttyACM0
endif
BAUD=115200

INO_FILE=CamCar.ino

WEBROOT=webroot
WEBROOT_FILES := $(wildcard $(WEBROOT)/*)
GEN=src/gen
GEN_FILES := $(patsubst $(WEBROOT)/%, $(GEN)/%_file.cpp, $(WEBROOT_FILES))
GEN_ENTRIES := $(GEN)/file-entries.cpp
ENV_FILE := .env
GEN_SECRETS := $(GEN)/secrets.h

.PHONY: all clean build install upload monitor tester test venv clean-venv

# Functional tests run against a *live* board over the network. Override the
# target with HOST, e.g. `make test HOST=camcar-840d8e.local` (the AI-Thinker)
# or `make test HOST=192.168.1.240`. Extra flags via TESTFLAGS, e.g.
# `make test TESTFLAGS="--with-xclk --clients 8"`.
HOST ?= camcar-f0f5bd.local
TESTFLAGS ?=

all: build

install:
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp32:esp32
	$(ARDUINO_CLI) lib install ESP32Servo
	$(ARDUINO_CLI) lib install "Async TCP"
	$(ARDUINO_CLI) lib install "ESP Async WebServer"

# Vendored, in-repo libraries not in the Arduino registry (e.g. Micro-RTSP,
# patched for runtime resolution changes). Each subdir is a library.
VENDOR_LIBS := libraries

build: gen-sources
	$(ARDUINO_CLI) compile --fqbn $(BOARD) --libraries $(VENDOR_LIBS) -e $(INO_FILE)

upload: build
	$(ARDUINO_CLI) upload --fqbn $(BOARD) --port $(PORT) $(INO_FILE)

monitor:
	$(ARDUINO_CLI) monitor --port $(PORT) --config "baudrate=$(BAUD),dtr=off,rts=off"

try: upload monitor

clean:
	rm -f $(GEN)
	rm -rf build/
	rm -f *.bin
	rm -f *.elf

ports:
	$(ARDUINO_CLI) board list

$(GEN):
	mkdir -p $(GEN)

$(GEN)/%_file.cpp: $(WEBROOT)/% | $(GEN)
	./scripts/file-entry.sh $<

$(GEN_ENTRIES): $(GEN_FILES)
	./scripts/file-system.sh

# Regenerate when .env changes; with no .env present this builds once with
# empty credential defaults (firmware then falls back to SoftAP setup mode).
$(GEN_SECRETS): $(wildcard $(ENV_FILE)) | $(GEN)
	./scripts/gen-secrets.sh

gen-sources: $(GEN_FILES) $(GEN_ENTRIES) $(GEN_SECRETS)

tester: $(VENV)/bin/activate
	(. $(VENV)/bin/activate; ./tools/tester.py)

# Functional tests against a live board (see HOST/TESTFLAGS above). Stdlib-only
# (plus ffmpeg for the RTSP tests, which skip cleanly if it is absent), so no
# venv is required.
test:
	$(PYTHON) tests/functional.py --host $(HOST) $(TESTFLAGS)

$(VENV)/bin/activate: tools/requirements.txt
	python3 -m venv $(VENV)
	$(PIP) install --upgrade pip
	$(PIP) install -r tools/requirements.txt

venv: $(VENV)/bin/activate

clean-venv:
	rm -rf $(VENV)


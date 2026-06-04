# Makefile for CamCar project

PYTHON=python3
ARDUINO_CLI=arduino-cli

# Board target: s3 (Freenove ESP32-S3-WROOM CAM, default) or cam (AI-Thinker
# ESP32-CAM). Override per-invocation, e.g. `make upload TARGET=cam`.
TARGET ?= s3
ifeq ($(TARGET),cam)
  # min_spiffs gives dual 1.9MB OTA app slots on the AI-Thinker's 4MB flash
  # (binary ~1.4MB fits with headroom), enabling the same web /update OTA as
  # the S3. One-time USB flash needed to lay down the OTA table.
  BOARD = esp32:esp32:esp32cam:PartitionScheme=min_spiffs
  PORT ?= /dev/ttyUSB0
else
  BOARD = esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=custom
  PORT ?= /dev/ttyACM0
endif

# The esp32 core's "custom" PartitionScheme reads ./partitions.csv from the
# sketch dir. Keep the source of truth in partitions/ and copy it in at build
# time (the root copy is a gitignored build artifact). S3 target only; the
# AI-Thinker CAM keeps its default scheme.
PARTITION_SRC := partitions/camcar_ota_16MB.csv
ifeq ($(TARGET),cam)
# The cam uses a built-in scheme (min_spiffs). A sketch-dir partitions.csv would
# OVERRIDE the FQBN PartitionScheme in the esp32 core, so a leftover one from a
# prior S3 build must be removed first.
PARTITION_DEP := no-partitions-csv
else
PARTITION_DEP := partitions.csv
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

.PHONY: all clean build install upload monitor test upload-ota

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

partitions.csv: $(PARTITION_SRC)
	cp $(PARTITION_SRC) partitions.csv

.PHONY: no-partitions-csv
no-partitions-csv:
	rm -f partitions.csv

build: gen-sources $(PARTITION_DEP)
	$(ARDUINO_CLI) compile --fqbn $(BOARD) --libraries $(VENDOR_LIBS) -e $(INO_FILE)

upload: build
	$(ARDUINO_CLI) upload --fqbn $(BOARD) --port $(PORT) $(INO_FILE)

# Wireless firmware update over WiFi: build, then POST the binary to the live
# board's /update endpoint (device password, no username). Override as needed:
#   make upload-ota HOST=camcar-f0f5bd.local DEVICE_PASS=secret
DEVICE_PASS ?= camcar
S3_BIN := build/esp32.esp32.esp32s3/CamCar.ino.bin

upload-ota: build
	curl --fail --progress-bar --user ":$(DEVICE_PASS)" \
	  -F firmware=@$(S3_BIN) http://$(HOST)/update
	@echo "\nFirmware posted; board rebooting into new image."

monitor:
	$(ARDUINO_CLI) monitor --port $(PORT) --config "baudrate=$(BAUD),dtr=off,rts=off"

try: upload monitor

clean:
	rm -rf $(GEN)
	rm -rf build/
	rm -f *.bin
	rm -f *.elf
	rm -f partitions.csv

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
$(GEN_SECRETS): $(wildcard $(ENV_FILE)) scripts/gen-secrets.sh | $(GEN)
	./scripts/gen-secrets.sh

gen-sources: $(GEN_FILES) $(GEN_ENTRIES) $(GEN_SECRETS)

# Functional tests against a live board (see HOST/TESTFLAGS above). Stdlib-only
# (plus ffmpeg for the RTSP tests, which skip cleanly if it is absent), so no
# venv or pip install is required.
test:
	$(PYTHON) tests/functional.py --host $(HOST) $(TESTFLAGS)


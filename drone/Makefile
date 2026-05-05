PODMAN ?= podman
SUDO ?= sudo
IDF_IMAGE ?= docker.io/espressif/idf:release-v5.0
PROJECT_DIR := $(CURDIR)
PORT ?= /dev/ttyUSB1
PODMAN_VOLUME_SUFFIX ?=
PODMAN_RUN_USER ?= --user 0
PODMAN_SECURITY_OPTS ?= --security-opt label=disable
CURRENT_UID := $(shell id -u)
CURRENT_GID := $(shell id -g)

PODMAN_BASE = $(PODMAN) run --rm $(PODMAN_SECURITY_OPTS) $(PODMAN_RUN_USER) -v "$(PROJECT_DIR)":/project$(PODMAN_VOLUME_SUFFIX) -w /project $(IDF_IMAGE)
PODMAN_FLASH = $(PODMAN) run --rm $(PODMAN_SECURITY_OPTS) $(PODMAN_RUN_USER) --device=$(PORT):$(PORT) -v "$(PROJECT_DIR)":/project$(PODMAN_VOLUME_SUFFIX) -w /project $(IDF_IMAGE)

.PHONY: target build flash flash-sudo clean fullclean menuconfig monitor fix-permissions

target:
	$(PODMAN_BASE) idf.py set-target esp32s2
	@$(SUDO) chown -R $(CURRENT_UID):$(CURRENT_GID) build/ sdkconfig* 2>/dev/null || true

build: fix-permissions
	$(PODMAN_BASE) idf.py build
	@$(SUDO) chown -R $(CURRENT_UID):$(CURRENT_GID) build/ sdkconfig* 2>/dev/null || true

flash: target
	$(PODMAN_FLASH) idf.py -p $(PORT) flash

flash-sudo:
	$(SUDO) $(PODMAN_BASE) idf.py set-target esp32s2
	$(SUDO) $(PODMAN_FLASH) idf.py -p $(PORT) flash

clean: fix-permissions
	$(PODMAN_BASE) idf.py clean

fullclean: fix-permissions
	$(PODMAN_BASE) idf.py fullclean

fix-permissions:
	@$(SUDO) chown -R $(CURRENT_UID):$(CURRENT_GID) build/ sdkconfig* 2>/dev/null || true
	@$(SUDO) chmod -R u+w build/ sdkconfig* 2>/dev/null || true

menuconfig: target
	$(PODMAN_BASE) idf.py menuconfig

monitor:
	$(PODMAN_FLASH) idf.py -p $(PORT) monitor
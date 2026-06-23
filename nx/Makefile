# =============================================================================
# OpenMoHAA - Nintendo Switch port: convenience wrapper around the CMake build.
#
#   make            configure (first time) + build  -> build/openmohaa.nro
#   make config     (re)run CMake configure step
#   make build      compile
#   make clean      remove the build directory
#   make install    copy the .nro to $(SWITCH_SD)/switch/openmohaa/
#
# Requires devkitPro with DEVKITPRO set in the environment, plus the portlib
# packages listed in README.md.
# =============================================================================

BUILD_DIR   ?= build
BUILD_TYPE  ?= Release
RENDERER    ?= gl1
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)
TOOLCHAIN   := cmake/toolchain/devkita64-libnx.cmake
NRO         := $(BUILD_DIR)/openmohaa.nro

# Destination SD root for `make install` (e.g. a mounted SD card)
SWITCH_SD   ?= /run/media/$(USER)/SWITCH

.PHONY: all config build clean install rebuild

all: build

$(BUILD_DIR)/CMakeCache.txt:
	@if [ -z "$(DEVKITPRO)" ]; then echo "ERROR: set DEVKITPRO (e.g. export DEVKITPRO=/opt/devkitpro)"; exit 1; fi
	cmake -S . -B $(BUILD_DIR) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
		-DNX_RENDERER=$(RENDERER)

config: $(BUILD_DIR)/CMakeCache.txt

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) -j $(JOBS)
	@echo ""
	@echo "Built: $(NRO)"

rebuild: clean all

clean:
	rm -rf $(BUILD_DIR)

install: build
	@mkdir -p "$(SWITCH_SD)/switch/openmohaa"
	cp "$(NRO)" "$(SWITCH_SD)/switch/openmohaa/openmohaa.nro"
	@echo "Installed to $(SWITCH_SD)/switch/openmohaa/openmohaa.nro"
	@echo "Remember to copy your MOHAA 'main' (and 'mainta'/'maintt') folders to"
	@echo "  $(SWITCH_SD)/switch/openmohaa/"

.DEFAULT_GOAL := build

.PHONY: build debug-build run debug-run clean format lint check-toolchain universal

# Retro68 build tree. The official Docker image ships one at /Retro68-build.
RETRO68_PATH ?= /Retro68-build
APP_NAME ?= App

# Apple's Universal Interfaces. Retro68's bundled Multiversal set has no
# Controls.h, Lists.h or Scrap.h, so the whole application is built against
# Universal; the staging script links it into the toolchain globally.
IAL_DIR ?= $(HOME)/Documents/SendToMacOS9/MPW/MPW-GM+PR+Final Updates/Interfaces&Libraries

TOOLCHAIN = $(RETRO68_PATH)/toolchain
TOOLCHAIN_PATH = $(TOOLCHAIN)/m68k-apple-macos/cmake/retro68.toolchain.cmake
LAUNCH_APPL = $(TOOLCHAIN)/bin/LaunchAPPL
STAGE_SCRIPT = $(RETRO68_PATH)/bin/interfaces-and-libraries.sh

CMAKE_OPTS = -DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) \
             -DAPP_NAME=$(APP_NAME) \
             -DLAUNCH_APPL=$(LAUNCH_APPL) \
             -DCMAKE_POLICY_VERSION_MINIMUM=3.5

check-toolchain:
	@if [ ! -f "$(TOOLCHAIN_PATH)" ]; then \
		echo "error: Retro68 toolchain not found at $(TOOLCHAIN_PATH)"; \
		echo "       Set RETRO68_PATH to a Retro68 build tree, e.g."; \
		echo "       make build RETRO68_PATH=\$$HOME/Projects/Retro68/build"; \
		exit 1; \
		fi

universal: check-toolchain
	@test -f "$(IAL_DIR)/Interfaces/CIncludes/OpenTransport.h" || { \
		echo "error: Apple's Universal Interfaces not found at $(IAL_DIR)"; \
		echo "       Set IAL_DIR=... (it must contain Interfaces/CIncludes and Libraries/Libraries)"; \
		exit 1; \
	}
	$(STAGE_SCRIPT) "$(TOOLCHAIN)" "$(IAL_DIR)" true false false
	@test -f "$(TOOLCHAIN)/m68k-apple-macos/include/Controls.h" || { \
		echo "error: staging did not install Controls.h"; \
		exit 1; \
	}

# One build at a time across the whole machine. This is not hypothetical: several
# agents share the retro68dev container, and a handful of parallel `cmake --build`
# jobs saturates every core and makes the host crawl. Both repos use the same
# lock file by default, so only one compile runs at a time anywhere.
BUILD_LOCK ?= /tmp/retro68-build.lock

build:
	@flock -w 3600 $(BUILD_LOCK) $(MAKE) --no-print-directory _build

_build: universal
	mkdir -p build dist
	cmake -S . -B build $(CMAKE_OPTS)
	cmake --build build
	cp build/$(APP_NAME).dsk dist/$(APP_NAME).dsk
	cp build/$(APP_NAME).bin dist/$(APP_NAME).bin
	cp build/$(APP_NAME).APPL dist/$(APP_NAME).APPL
	cp build/$(APP_NAME).ad dist/$(APP_NAME).ad
	cp "build/%$(APP_NAME).ad" "dist/%$(APP_NAME).ad"

debug-build: universal
	mkdir -p build dist
	cmake -S . -B build $(CMAKE_OPTS) -DCMAKE_BUILD_TYPE=Debug
	cmake --build build
	cp build/$(APP_NAME).dsk dist/$(APP_NAME).dsk
	cp build/$(APP_NAME).bin dist/$(APP_NAME).bin
	cp build/$(APP_NAME).APPL dist/$(APP_NAME).APPL
	cp build/$(APP_NAME).ad dist/$(APP_NAME).ad
	cp "build/%$(APP_NAME).ad" "dist/%$(APP_NAME).ad"

run: build
	cmake --build build --target run

debug-run: debug-build
	cmake --build build --target run

clean:
	rm -rf build dist

# Format the code with clang-format
format:
	@echo "Formatting code using clang-format..."
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found, please install it."; exit 1; }
	@find src \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print | xargs clang-format -i -style=file
	@echo "Formatting complete."

# Run static analysis
lint: universal
	@echo "Running static analysis..."
	@cmake -S . -B build $(CMAKE_OPTS) -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null
	if command -v clang-tidy >/dev/null 2>&1; then \
		echo "Found clang-tidy, performing deep analysis..."; \
		for file in $$(find src \( -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" \) -print); do \
			echo "Analyzing $$file"; \
			clang-tidy "$$file" -p build; \
		done; \
		echo ""; \
	fi
	@echo "Static analysis complete."

# Usage examples:
# make build RETRO68_PATH=/path/to/Retro68-build
# make build APP_NAME=MacCraft
# make run
# make format
# make lint

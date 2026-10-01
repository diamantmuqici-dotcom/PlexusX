# PlexusX — Windows x64 Build
# Cross-compiles with zig cc (target: x86_64-windows-gnu)
#
# GUI subsystem: the executables are LINKED as GUI programs with -Wl,--subsystem,windows.  That makes the
# linker pick the GUI CRT start-up (WinMainCRTStartup, __mingw_app_type = 1) for wWinMain.  Never flip the
# subsystem byte of a console-linked binary afterwards: the console start-up (wmainCRTStartup path) would run
# inside a GUI-flagged image and abort at launch.  `make` ends with tools/verify_pe.py, which checks the PE
# subsystem (2) and the entry point of both executables.
CC      := python3 -m ziglang cc
CFLAGS  ?= -target x86_64-windows-gnu -O2 -std=c11 -Wall \
           -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -municode \
           -Isrc -Isrc/color -Isrc/display -Isrc/games -Isrc/settings -Isrc/windows -Isrc/diagnostics -Isrc/core -Isrc/security -Isrc/presets -Isrc/ui -Isrc/logging
LDFLAGS ?= -Wl,--subsystem,windows
LIBS    := -luser32 -lgdi32 -lshell32 -ladvapi32 -lws2_32 -lcomctl32 -lcomdlg32 -lshlwapi -ldwmapi -lwtsapi32 -lm
SRC     := $(shell find src -name '*.c' | sort)
HDR     := $(shell find src -name '*.h' | sort)
OUT_DIR := ../site/download
OUT_EXE := $(OUT_DIR)/PlexusX.exe
OUT_SET := $(OUT_DIR)/PlexusX-Setup.exe

all: $(OUT_EXE) $(OUT_SET) verify

# Linked into build/ (the linker also drops a .pdb next to its output), then installed byte-for-byte (no patching).
$(OUT_EXE): $(SRC) $(HDR)
	@mkdir -p $(OUT_DIR) build
	$(CC) $(CFLAGS) $(LDFLAGS) -o build/PlexusX.exe $(SRC) $(LIBS)
	install -m 0644 build/PlexusX.exe $(OUT_EXE)
	@echo "Built $(OUT_EXE)"

$(OUT_SET): $(OUT_EXE) tools/setup.c tools/build_setup.py
	python3 tools/build_setup.py $(OUT_EXE) $(OUT_SET)
	@echo "Built $(OUT_SET)"

verify: $(OUT_EXE) $(OUT_SET)
	python3 tools/verify_pe.py $(OUT_EXE) $(OUT_SET)

clean:
	rm -rf build $(OUT_EXE) $(OUT_SET)

.PHONY: all clean verify

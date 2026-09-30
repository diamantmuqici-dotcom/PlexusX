# ChromaX — Windows x64 build (cross-compiles from Linux/macOS via `zig cc`)
# plus native test target for the platform-independent core.
#
#   make            -> ../dist/ChromaX-2.0.0-windows-x64/ChromaX.exe
#   make site       -> also copies the exe to ../site/download/ChromaX.exe
#   make test       -> builds & runs the core unit tests with the native C compiler

ZIGPY   ?= python3
CC      := $(ZIGPY) -m ziglang cc
CFLAGS  ?= -target x86_64-windows-gnu -O2 -std=c11 -Wall -Wextra \
           -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -municode
LIBS    := -luser32 -lgdi32 -lshell32 -ladvapi32 -lws2_32 -lcomdlg32

VER     := 2.0.0
DIST    := ../dist/ChromaX-$(VER)-windows-x64
OUT     := $(DIST)/ChromaX.exe

CORE    := src/cx_color.c src/cx_json.c src/cx_preset.c src/cx_games.c \
           src/cx_hotkey.c src/cx_backup.c src/cx_scene.c
WIN     := src/main.c src/ui.c src/engine.c src/monitors.c src/modes.c \
           src/hdr.c src/gamewatch.c src/crosshair.c src/phone.c src/diag.c \
           src/preview.c
SRC     := $(CORE) $(WIN)

NATIVE_CC ?= cc

all: $(OUT)

$(OUT): $(SRC) src/common.h src/cx_color.h src/cx_json.h src/cx_preset.h \
        src/cx_games.h src/cx_hotkey.h src/cx_backup.h src/cx_scene.h
	@mkdir -p $(DIST) build
	$(CC) $(CFLAGS) -o build/chromax.exe $(SRC) $(LIBS)
	$(ZIGPY) tools/fixsub.py build/chromax.exe $(OUT)
	@echo "built $(OUT)"

site: $(OUT)
	@mkdir -p ../site/download
	cp $(OUT) ../site/download/ChromaX.exe
	cd ../site/download && sha256sum ChromaX.exe > SHA256SUMS.txt
	@cat ../site/download/SHA256SUMS.txt

test:
	@mkdir -p build
	$(NATIVE_CC) -O2 -std=c11 -Wall -Wextra -D_GNU_SOURCE -Isrc \
	  tests/test_core.c $(CORE) -lm -o build/test_core
	./build/test_core

clean:
	rm -rf build

.PHONY: all site test clean

# PlexusX — Windows x64 Build
# Cross-compiles with zig cc (target: x86_64-windows-gnu)
CC      := python3 -m ziglang cc
CFLAGS  ?= -target x86_64-windows-gnu -O2 -std=c11 -Wall \
           -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -municode
LIBS    := -luser32 -lgdi32 -lshell32 -ladvapi32 -lws2_32 -lcomctl32 -lshlwapi -lm
SRC     := $(wildcard src/*.c)
OUT_DIR := ../site/download
OUT_EXE := $(OUT_DIR)/PlexusX.exe
OUT_SET := $(OUT_DIR)/PlexusX-Setup.exe

all: $(OUT_EXE) $(OUT_SET)

$(OUT_EXE): $(SRC) src/common.h
	@mkdir -p $(OUT_DIR) build
	$(CC) $(CFLAGS) -o build/plexusx-gui.exe $(SRC) $(LIBS)
	python3 tools/fixsub.py build/plexusx-gui.exe $(OUT_EXE)
	@echo "Built $(OUT_EXE)"

$(OUT_SET): $(OUT_EXE) tools/setup.c tools/build_setup.py
	python3 tools/build_setup.py $(OUT_EXE) $(OUT_SET)
	@echo "Built $(OUT_SET)"

clean:
	rm -rf build $(OUT_EXE) $(OUT_SET)

.PHONY: all clean

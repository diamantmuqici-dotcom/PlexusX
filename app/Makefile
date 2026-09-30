# ChromaX — Windows build (zig cc cross-compiles from Linux/macOS; mingw-w64 also works)
CC      := python3 -m ziglang cc
CFLAGS  ?= -target x86_64-windows-gnu -O2 -std=c11 -Wall \
           -DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -municode
LIBS    := -luser32 -lgdi32 -lshell32 -ladvapi32 -lws2_32
SRC     := $(wildcard src/*.c)
OUT     := ../site/download/ChromaX.exe

all: $(OUT)

$(OUT): $(SRC) src/common.h
	@mkdir -p ../site/download build
	$(CC) $(CFLAGS) -o build/chromax-gui.exe $(SRC) $(LIBS)
	python3 tools/fixsub.py build/chromax-gui.exe $(OUT)
	@echo "built $(OUT)"

clean:
	rm -rf build

.PHONY: all clean

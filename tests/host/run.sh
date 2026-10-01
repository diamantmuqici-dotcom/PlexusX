#!/bin/sh
# Builds the REAL ColorEngine + ColorPipeline (app/src/color/*) against a mocked Win32 display
# stack (tests/host/shim) and runs the
# scenario tests, under AddressSanitizer + UBSan.  Needs only gcc/clang on Linux or macOS.
#
#   sh tests/host/run.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../app/src"
tmp=$(mktemp -d "${TMPDIR:-/tmp}/plexusx-host.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

${CC:-gcc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -g -O1 \
    -fsanitize=address,undefined -fno-sanitize-recover=all \
    -I"$here/shim" -I"$src" -I"$here" \
    -I"$src/color" -I"$src/display" -I"$src/games" -I"$src/settings" -I"$src/windows" -I"$src/diagnostics" \
    -o "$tmp/engine_host_test" \
    "$here/engine_host_test.c" "$here/host_mock.c" "$src/color/color_engine.c" "$src/color/color_pipeline.c" -lm
"$tmp/engine_host_test" "$tmp/work"

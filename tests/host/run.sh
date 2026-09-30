#!/bin/sh
# Builds the REAL app/src/engine.c against a mocked Win32 display stack (tests/host/shim) and runs the
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
    -o "$tmp/engine_host_test" \
    "$here/engine_host_test.c" "$here/host_mock.c" "$src/engine.c" -lm
"$tmp/engine_host_test" "$tmp/work"

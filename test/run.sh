#!/usr/bin/env bash
# Runs the native stub tests (Linux or WSL; needs 32-bit execution support, which WSL2 has).
# First generate the image:  python tools/make_test_image.py <path to gta-sa.exe> build/steam_reloc.bin
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG="${ZIG:-zig}"
mkdir -p build
$ZIG cc -target x86-linux-musl -static -O1 -Wall -Itest/shim -o build/harness test/harness.c
cd build && ./harness steam_reloc.bin

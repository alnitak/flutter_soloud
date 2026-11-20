#!/bin/bash
# The Apple full decoder (os_decoder_apple.mm, LoadMode.memory) across the
# formats afconvert writes: ALAC (M4A, CAF), AIFF, CAF integer/float PCM,
# AAC-LC and HE-AAC (M4A, CAF); 1-6 channels; 8-96 kHz; 16/24-bit sources.
# Builds and runs test/macos/os_decoder_apple_formats_test.cpp, which encodes
# each case with afconvert and compares length, samples (lossless) or
# per-channel tones (lossy) with the source, and decodeMemory with decodeFile.
#
#   test/macos/os_decoder_apple_formats_test.sh [--asan]
#
# Platform: macOS. Needs: Xcode command line tools and afconvert (part of
# macOS); prints SKIPPED without them. No assets, no audio device.
#   --asan  build with AddressSanitizer and UBSan.
# Verification: verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIPPED: macOS only"
    exit 0
fi
if ! command -v afconvert > /dev/null; then
    echo "SKIPPED: afconvert not found"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/os_decoder_apple_formats_test"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

clang++ -std=c++17 -O2 -Wall "${SANITIZER[@]}" \
    -I src \
    -I src/native_decoder \
    -o "$OUT" \
    test/macos/os_decoder_apple_formats_test.cpp \
    src/native_decoder/os_decoder_apple.mm \
    -framework AudioToolbox -framework CoreFoundation

"$OUT" "$WORK_DIR"

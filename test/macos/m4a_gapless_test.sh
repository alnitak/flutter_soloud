#!/bin/bash
# Gapless M4A (iTunSMPB) on macOS: encodes exact-length tones with afconvert
# (AAC-LC, HE-AAC, HE-AAC v2) and checks that m4a_gapless.cpp, the full decode
# (os_decoder_apple.mm) and the streaming backend
# (native_stream_decoder_apple.cpp) all trim priming and padding to the exact
# source length. Builds and runs test/macos/m4a_gapless_test.cpp.
#
#   test/macos/m4a_gapless_test.sh [--asan]
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

OUT="${TMPDIR:-/tmp}/m4a_gapless_test"
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
    test/macos/m4a_gapless_test.cpp \
    src/native_stream/m4a_gapless.cpp \
    src/native_stream/native_stream_decoder_apple.cpp \
    src/native_decoder/os_decoder_apple.mm \
    -framework AudioToolbox -framework CoreFoundation

"$OUT" "$WORK_DIR"

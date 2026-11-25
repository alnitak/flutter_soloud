#!/bin/bash
# AudioToolbox decoders on macOS: NativeAudioDecoder (os_decoder_apple.mm) and
# the streaming AACDecoderWrapper (aac_stream_decoder_apple.mm) for ADTS AAC,
# HE-AAC, AC-3 and E-AC-3. Builds and runs
# test/macos/aac_stream_decoder_apple_test.mm, which runs the shared checks in
# test/support/os_decoder_checks.h plus Apple-only stream checks.
#
#   test/macos/aac_stream_decoder_apple_test.sh [--asan]
#
# Platform: macOS. Needs: Xcode command line tools, the fixtures in
# example/assets/audio, and afconvert for the HE-AAC fixture (that check is
# not run without it). No audio device. Prints SKIPPED off macOS.
#   --asan  build with AddressSanitizer and UBSan.
# Verification: verified on macOS arm64. The same source runs on the iOS
# simulator through test/ios/aac_stream_decoder_apple_test.sh.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIPPED: macOS only"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/aac_stream_decoder_apple_test"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

clang++ -std=c++17 -O2 -Wall -fobjc-arc "${SANITIZER[@]}" \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/macos/aac_stream_decoder_apple_test.mm \
    src/native_decoder/os_decoder_apple.mm \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_apple.mm \
    src/audiobuffer/m4a_metadata.cpp \
    -framework AudioToolbox -framework CoreFoundation -framework Foundation

HE_AAC=""
if command -v afconvert > /dev/null; then
    HE_AAC="$WORK_DIR/he_aac.aac"
    afconvert -f adts -d aach example/assets/audio/sample-WAV.wav "$HE_AAC"
fi

"$OUT" example/assets/audio "$WORK_DIR" "$HE_AAC"

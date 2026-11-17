#!/bin/bash
# Build & run the standalone native decoder correctness tests.
# Run from the flutter_soloud repo root.
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/native_decoder_test"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

PLATFORM_SRCS=()
PLATFORM_LIBS=()
if [ "$(uname -s)" = "Darwin" ]; then
    PLATFORM_SRCS+=(src/audiobuffer/aac_stream_decoder_apple.mm src/native_decoder/os_decoder_apple.mm)
    PLATFORM_LIBS+=(-framework AudioToolbox -framework CoreFoundation)
else
    PLATFORM_SRCS+=(src/audiobuffer/aac_stream_decoder_linux.cpp src/native_decoder/os_decoder_linux.cpp)
    PLATFORM_LIBS+=(-ldl)
fi

c++ -std=c++17 -O2 -Wall "${SANITIZER[@]}" \
    -I src \
    -I src/soloud/include \
    -I src/native_decoder \
    -I src/audiobuffer \
    -I src/soloud/src/audiosource/wav \
    -o "$OUT" \
    test/native_decoder_test.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    "${PLATFORM_SRCS[@]}" \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    "${PLATFORM_LIBS[@]}"

"$OUT"

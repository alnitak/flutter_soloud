#!/bin/bash
# Build and run the standalone native tests for NativeStream, the
# LoadMode.disk streaming source for M4A/MP4 (src/native_stream/). Uses the
# null backend, so no audio hardware is needed.
#
# On macOS the ExtAudioFile backend is compared with the full decode, also on
# an Apple-encoded (iTunSMPB) file made with afconvert when available.
#
# Pass --tsan to build under ThreadSanitizer instead of AddressSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-fsanitize=address -fno-omit-frame-pointer)
OUT="${TMPDIR:-/tmp}/native_stream_test"
if [ "$1" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread)
    OUT="${OUT}_tsan"
fi

PLATFORM_SOURCES=()
PLATFORM_LIBS=()
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM_SOURCES=(src/native_decoder/os_decoder_apple.mm)
    PLATFORM_LIBS=(-framework AudioToolbox -framework CoreFoundation)
fi

c++ -std=c++17 -O1 -g -Wall -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src/soloud/include \
    -I src \
    -I src/native_decoder \
    -o "$OUT" \
    test/native_stream_test.cpp \
    src/native_stream/*.cpp \
    "${PLATFORM_SOURCES[@]}" \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${PLATFORM_LIBS[@]}"

# Files that decode deterministically wherever decoding starts (see the
# test): Apple's encoder, and ffmpeg's without noise substitution.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
APPLE_ENCODED=""
FFMPEG_ENCODED=""
AC3_IN_MP4=""
NO_EDIT_LIST=""
if command -v afconvert > /dev/null; then
    APPLE_ENCODED="$WORK_DIR/apple_encoded.m4a"
    afconvert -f m4af -d aac -b 128000 example/assets/audio/sample-WAV.wav "$APPLE_ENCODED"
fi
if command -v ffmpeg > /dev/null; then
    FFMPEG_ENCODED="$WORK_DIR/ffmpeg_encoded.m4a"
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a aac -aac_pns 0 -b:a 128k "$FFMPEG_ENCODED"
    AC3_IN_MP4="$WORK_DIR/ac3.mp4"
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a ac3 "$AC3_IN_MP4"
    NO_EDIT_LIST="$WORK_DIR/no_edit_list.m4a"
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a aac -b:a 128k -use_editlist 0 \
        "$NO_EDIT_LIST"
fi

"$OUT" example/assets/audio "$APPLE_ENCODED" "$FFMPEG_ENCODED" "$AC3_IN_MP4" "${NO_EDIT_LIST:-}"

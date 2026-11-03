#!/bin/bash
# Build the NativeStream tests (test/native_stream_test.cpp) with the NDK and
# run them on a connected device or emulator, exercising the MediaExtractor +
# MediaCodec streaming backend against the full MediaCodec decode.
# Run from the flutter_soloud repo root:
#   test/run_android_native_stream_test.sh [adb-serial]
# Env: ANDROID_NDK_HOME (defaults to the newest NDK in ~/Library/Android/sdk/ndk
# or $ANDROID_HOME/ndk), ABI (defaults to the device's primary ABI).
# Needs ffmpeg and/or afconvert on the host for the deterministic test files.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

ADB=(adb)
if [ -n "$1" ]; then
    ADB=(adb -s "$1")
fi

SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* | sort -V | tail -1)}"
HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
ABI="${ABI:-$("${ADB[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')}"
case "$ABI" in
    arm64-v8a) TARGET=aarch64-linux-android21 ;;
    armeabi-v7a) TARGET=armv7a-linux-androideabi21 ;;
    x86_64) TARGET=x86_64-linux-android21 ;;
    x86) TARGET=i686-linux-android21 ;;
    *) echo "Unsupported ABI: $ABI"; exit 1 ;;
esac

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

OUT="$WORK_DIR/native_stream_test"
"$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang++" --target="$TARGET" \
    -std=c++17 -O2 -Wall -static-libstdc++ \
    -DWITH_NULL \
    -I src/soloud/include \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/native_stream_test.cpp \
    src/native_stream/*.cpp \
    src/native_decoder/os_decoder_android.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_android.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    -lmediandk -llog -ldl

# Files that decode deterministically wherever decoding starts (see the test).
FILES=(example/assets/audio/sample-AAC.m4a example/assets/audio/aac_lc_mono_44100.m4a
       example/assets/audio/aac_lc_stereo_48000.m4a example/assets/audio/sample-MP3.mp3)
APPLE_ENCODED=""
FFMPEG_ENCODED=""
AC3_IN_MP4=""
NO_EDIT_LIST=""
if command -v afconvert > /dev/null; then
    afconvert -f m4af -d aac -b 128000 example/assets/audio/sample-WAV.wav "$WORK_DIR/apple_encoded.m4a"
    FILES+=("$WORK_DIR/apple_encoded.m4a")
    APPLE_ENCODED=apple_encoded.m4a
fi
if command -v ffmpeg > /dev/null; then
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a aac -aac_pns 0 -b:a 128k \
        "$WORK_DIR/ffmpeg_encoded.m4a"
    FILES+=("$WORK_DIR/ffmpeg_encoded.m4a")
    FFMPEG_ENCODED=ffmpeg_encoded.m4a
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a ac3 "$WORK_DIR/ac3.mp4"
    FILES+=("$WORK_DIR/ac3.mp4")
    AC3_IN_MP4=ac3.mp4
    ffmpeg -loglevel error -y -i example/assets/audio/sample-WAV.wav -c:a aac -b:a 128k -use_editlist 0 \
        "$WORK_DIR/no_edit_list.m4a"
    FILES+=("$WORK_DIR/no_edit_list.m4a")
    NO_EDIT_LIST=no_edit_list.m4a
fi

DEVICE_DIR=/data/local/tmp/soloud_native_stream_test
"${ADB[@]}" shell "rm -rf $DEVICE_DIR && mkdir -p $DEVICE_DIR/tmp"
"${ADB[@]}" push "$OUT" "$DEVICE_DIR/" > /dev/null
"${ADB[@]}" push "${FILES[@]}" "$DEVICE_DIR/" > /dev/null
"${ADB[@]}" shell "cd $DEVICE_DIR && TMPDIR=$DEVICE_DIR/tmp ./native_stream_test $DEVICE_DIR \
    '${APPLE_ENCODED:+$DEVICE_DIR/$APPLE_ENCODED}' '${FFMPEG_ENCODED:+$DEVICE_DIR/$FFMPEG_ENCODED}' \
    '${AC3_IN_MP4:+$DEVICE_DIR/$AC3_IN_MP4}' '${NO_EDIT_LIST:+$DEVICE_DIR/$NO_EDIT_LIST}'"

#!/bin/bash
# Build the Android MediaCodec decoder tests (os_decoder_android.cpp,
# aac_stream_decoder_android.cpp) with the NDK and run them on a connected
# device or emulator over adb. Covers M4A/ADTS/AC-3/E-AC-3 decodeFile and
# decodeMemory, malformed input, chunked streaming through AACDecoderWrapper
# (AAC, AC-3, E-AC-3), descriptor/temp-file cleanup and concurrency; see
# test/android/android_native_decoder_test.cpp.
# Run from the flutter_soloud repo root:
#   test/android/android_native_decoder_test.sh [adb-serial]
#   ABI=x86_64 test/android/android_native_decoder_test.sh --build-only
# Platform: any host with the NDK; a device/emulator (adb) to run. Fixtures
# from example/assets/audio are pushed to /data/local/tmp.
# Env: ANDROID_NDK_HOME (defaults to the newest NDK in ~/Library/Android/sdk/ndk
# or $ANDROID_HOME/ndk), ABI (defaults to the device's primary ABI, or
# arm64-v8a with --build-only).
#   --build-only  only build (no adb needed); prints the binary's path.
# Without --build-only and no adb or device the script prints SKIPPED.
# Verification: --build-only verified with NDK 29 (macOS host); the on-device
# run predates the test reorganization and was not re-run here.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

BUILD_ONLY=0
if [ "${1:-}" == "--build-only" ]; then
    BUILD_ONLY=1
    shift
fi

ADB=(adb)
if [ -n "${1:-}" ]; then
    ADB=(adb -s "$1")
fi
if [ "$BUILD_ONLY" == "0" ]; then
    if ! command -v adb > /dev/null; then
        echo "SKIPPED: adb not found (use --build-only to just build)"
        exit 0
    fi
    if ! "${ADB[@]}" get-state > /dev/null 2>&1; then
        echo "SKIPPED: no Android device or emulator connected"
        exit 0
    fi
fi

SDK="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* | sort -V | tail -1)}"
HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
if [ "$BUILD_ONLY" == "1" ]; then
    ABI="${ABI:-arm64-v8a}"
else
    ABI="${ABI:-$("${ADB[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')}"
fi
case "$ABI" in
    arm64-v8a) TARGET=aarch64-linux-android21 ;;
    armeabi-v7a) TARGET=armv7a-linux-androideabi21 ;;
    x86_64) TARGET=x86_64-linux-android21 ;;
    x86) TARGET=i686-linux-android21 ;;
    *) echo "Unsupported ABI: $ABI"; exit 1 ;;
esac

OUT="${TMPDIR:-/tmp}/android_native_decoder_test"
"$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin/clang++" --target="$TARGET" \
    -std=c++17 -O2 -Wall -static-libstdc++ \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/android/android_native_decoder_test.cpp \
    src/native_decoder/os_decoder_android.cpp \
    src/native_stream/m4a_gapless.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_android.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    -lmediandk -llog -ldl

if [ "$BUILD_ONLY" == "1" ]; then
    echo "Built $ABI: $OUT"
    exit 0
fi

DEVICE_DIR=/data/local/tmp/soloud_decoder_test
"${ADB[@]}" shell "rm -rf $DEVICE_DIR && mkdir -p $DEVICE_DIR/tmp"
"${ADB[@]}" push "$OUT" "$DEVICE_DIR/" > /dev/null
"${ADB[@]}" push example/assets/audio/aac_lc_mono_44100.m4a example/assets/audio/aac_lc_stereo_48000.m4a \
    example/assets/audio/sample-AAC.aac example/assets/audio/sample-AC3.ac3 \
    example/assets/audio/sample-EAC3.eac3 "$DEVICE_DIR/" > /dev/null
"${ADB[@]}" shell "cd $DEVICE_DIR && TMPDIR=$DEVICE_DIR/tmp ./android_native_decoder_test $DEVICE_DIR"

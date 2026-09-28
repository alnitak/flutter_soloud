#!/bin/bash
# Build the AAudio reroute race test for Android and run it on a device or
# emulator over adb.
#
#   ./test/run_aaudio_reroute_race_test.sh [seconds] [operations]
#
# Uses the newest NDK under $ANDROID_HOME/ndk unless ANDROID_NDK_HOME is set,
# and the adb device in $ANDROID_SERIAL when more than one is attached.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SECONDS_TO_RUN="${1:-30}"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)}"
HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
API=31

case "$(adb shell getprop ro.product.cpu.abi | tr -d '\r')" in
    arm64-v8a) TARGET=aarch64-linux-android ;;
    x86_64) TARGET=x86_64-linux-android ;;
    *) echo "unsupported device ABI"; exit 1 ;;
esac

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
OUT="${TMPDIR:-/tmp}/aaudio_reroute_race_test"
DEVICE_PATH=/data/local/tmp/aaudio_reroute_race_test

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
"$TOOLCHAIN/$TARGET$API-clang" -std=gnu99 -O3 -c -w \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# Same optimization and defines as the plugin build (hook/build.dart).
"$TOOLCHAIN/$TARGET$API-clang++" -std=c++17 -O3 -ffast-math \
    -fvisibility=hidden -w \
    -DWITH_MINIAUDIO -DNDEBUG -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src/soloud/src \
    -I src/pffft \
    -I src \
    -static-libstdc++ \
    -o "$OUT" \
    test/aaudio_reroute_race_test.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud_common.cpp \
    src/analyzer.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    -ldl -lm -llog

adb push "$OUT" "$DEVICE_PATH" >/dev/null
adb shell "$DEVICE_PATH" "$SECONDS_TO_RUN" "${2:-SPXR}"

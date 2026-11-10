#!/bin/bash
# Build the AAudio close race test for Android and run it on a device or
# emulator over adb. See test/aaudio_close_race_test.cpp; the crash it guards
# against is specific to Android 11 and earlier.
#
#   ./test/run_aaudio_close_race_test.sh [seconds] [threads] [device] [device]
#   ANDROID_ABI=x86_64 ANDROID_API=30 ./test/run_aaudio_close_race_test.sh --build-only
#
# Uses the newest NDK under $ANDROID_HOME/ndk unless ANDROID_NDK_HOME is set,
# and the adb device in $ANDROID_SERIAL when more than one is attached.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_ONLY=0
if [ "${1:-}" == "--build-only" ]; then
    BUILD_ONLY=1
    shift
fi

ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)}"
HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
# AAudio's NDK API needs 26; build for the device's own level, capped at 31.
if [ "$BUILD_ONLY" == "1" ]; then
    API="${ANDROID_API:-30}"
    TEST_ABI="${ANDROID_ABI:-arm64-v8a}"
else
    DEVICE_API="$(adb shell getprop ro.build.version.sdk | tr -d '\r')"
    API=$(( DEVICE_API < 31 ? DEVICE_API : 31 ))
    TEST_ABI="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
fi

case "$TEST_ABI" in
    arm64-v8a) TARGET=aarch64-linux-android ;;
    armeabi-v7a) TARGET=armv7a-linux-androideabi ;;
    x86_64) TARGET=x86_64-linux-android ;;
    x86) TARGET=i686-linux-android ;;
    *) echo "unsupported device ABI"; exit 1 ;;
esac

TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
OUT="${AAUDIO_TEST_OUT:-${TMPDIR:-/tmp}/aaudio_close_race_test}"
DEVICE_PATH=/data/local/tmp/aaudio_close_race_test

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
"$TOOLCHAIN/$TARGET$API-clang" -std=gnu99 -O3 -c -w \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# Same optimization and defines as the plugin build (hook/build.dart), and the
# engine's own miniaudio backend, whose AAudioStream_close is under test.
"$TOOLCHAIN/$TARGET$API-clang++" -std=c++17 -O3 -ffast-math \
    -fvisibility=hidden -w \
    -DWITH_MINIAUDIO -DNDEBUG -DNO_XIPH_LIBS -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    -I src/soloud/include \
    -I src/soloud/src \
    -I src/pffft \
    -I src \
    -static-libstdc++ \
    -o "$OUT" \
    test/aaudio_close_race_test.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud_common.cpp \
    src/device_lifecycle_test_hooks.cpp \
    src/analyzer.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    -laaudio -ldl -lm -llog

if [ "$BUILD_ONLY" == "1" ]; then
    echo "Built $TEST_ABI API $API: $OUT"
    exit 0
fi

adb push "$OUT" "$DEVICE_PATH" >/dev/null
adb shell "$DEVICE_PATH" "$@"

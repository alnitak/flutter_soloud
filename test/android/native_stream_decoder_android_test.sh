#!/bin/bash
# Streaming M4A/MP4 decoding on Android (LoadMode.disk):
# src/native_stream/native_stream_decoder_android.cpp (AMediaExtractor +
# AMediaCodec) with m4a_gapless.cpp. Builds
# test/android/native_stream_decoder_android_test.cpp with the NDK and runs it
# on a device or emulator over adb. Covers AAC-LC, HE-AAC, HE-AAC v2 (made
# with afconvert), AC-3 in MP4 (ffmpeg, optional), the bundled M4A assets,
# exact length and tone onset, seek accuracy, end of data, truncated files,
# unsupported input (MP3, no audio track, garbage) and pinned descriptors.
#
#   test/android/native_stream_decoder_android_test.sh [--build-only] [--strict] [adb-serial]
#
# Platform: any host with the NDK; a device/emulator (adb) to run. The
# fixtures are made on the host: afconvert (macOS) for the HE-AAC and exact
# length cases, ffmpeg (optional) for AC-3/faststart/truncated cases; cases
# whose tool is missing are left out. No audio device needed.
# Env: see test/support/android_ndk.sh (ANDROID_NDK_HOME, ABI, API, ANDROID_SERIAL);
# CASE_FILTER=<regex> runs only the matching fixtures.
#   --build-only  only build (no adb needed); prints the binary's path.
#   --strict      count KNOWN BUG checks (L3) as failures.
# Without --build-only and no adb or device the script prints SKIPPED.
# Verification: run on a Pixel 8 API 34 arm64 emulator (macOS arm64 host, NDK 29).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/android_ndk.sh

android_parse_args "$@"
android_require || exit 0
android_target

OUT="${TMPDIR:-/tmp}/native_stream_decoder_android_test"
"${CXX[@]}" -std=c++17 -O2 -Wall -static-libstdc++ \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/android/native_stream_decoder_android_test.cpp \
    src/native_stream/native_stream_decoder_android.cpp \
    src/native_stream/m4a_gapless.cpp \
    src/native_decoder/os_decoder_android.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_android.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    -lmediandk -llog -ldl

if [ "$BUILD_ONLY" == "1" ]; then
    echo "Built $ABI (API $API): $OUT"
    exit 0
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT
. test/support/native_stream_fixtures.sh
FIX="$WORK_DIR/fixtures"
make_native_stream_fixtures "$FIX" "$WORK_DIR"

DEVICE_DIR=/data/local/tmp/soloud_native_stream_test
"${ADB[@]}" shell "rm -rf $DEVICE_DIR && mkdir -p $DEVICE_DIR"
"${ADB[@]}" push "$FIX/." "$DEVICE_DIR/" > /dev/null
set +e
android_run "$OUT" "${ANDROID_ARGS[@]}" "$DEVICE_DIR"
code=$?
set -e
"${ADB[@]}" shell "rm -rf $DEVICE_DIR"
exit $code

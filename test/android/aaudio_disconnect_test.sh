#!/bin/bash
# AAudio device disconnection (headphones unplugged) on Android: builds
# test/android/aaudio_disconnect_test.cpp with the whole engine and runs it
# over adb. The test reports AAUDIO_ERROR_DISCONNECTED through the engine's
# stream error gate the way AAudio does and checks that the stream is
# replaced, the voice keeps mixing at real time (one stream), Dart gets
# "rerouted", a stopped device stays stopped, stale errors are ignored, and a
# disconnect racing deinit neither crashes nor hangs.
#
#   test/android/aaudio_disconnect_test.sh [--build-only] [--strict] [adb-serial]
#
# Platform: any host with the NDK; a device or emulator with AAudio (API 30+;
# the engine uses OpenSL below) to run. Opens the real audio output (a quiet
# sine). Prints SKIPPED without the NDK, adb, a device, or on API < 30.
# Env: see test/support/android_ndk.sh.
# Verification: run on a Pixel 8 API 34 arm64 emulator (macOS arm64 host, NDK 29).

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/android_engine_test.sh
android_parse_args "$@"
if [ "$BUILD_ONLY" == "0" ] && android_require > /dev/null && [ "$(android_device_api)" -lt 30 ]; then
    echo "SKIPPED: the engine uses OpenSL below API 30 (device API $(android_device_api))"
    exit 0
fi
android_engine_test aaudio_disconnect_test 21 -- "$@"

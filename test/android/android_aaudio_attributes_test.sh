#!/bin/bash
# setAndroidAAudioAttributes() on a real AAudio stream: builds
# test/android/android_aaudio_attributes_test.cpp with the whole engine and
# runs it over adb. Reads the engine's stream usage/contentType/capture
# policy back from AAudio: media, voice communication, invalid values,
# unmanaged (no setter called), a call after init (applied by changeDevice
# only), a reroute keeping them, and lowLatency leaving them alone. The Dart
# side (no effect off Android, not-initialized error) is
# test/android/android_aaudio_attributes_test.dart.
#
#   test/android/android_aaudio_attributes_test.sh [--build-only] [--strict] [adb-serial]
#
# Platform: any host with the NDK; a device or emulator with AAudio (API 30+)
# to run. Opens the real audio output (a quiet sine). Prints SKIPPED without
# the NDK, adb, a device, or on API < 30. Env: see test/support/android_ndk.sh.
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
android_engine_test android_aaudio_attributes_test 21 -- "$@"

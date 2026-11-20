#!/bin/bash
# OpenSL ES fallback on Android <= 10: init_platform_context() in
# soloud_miniaudio.cpp picks OpenSL alone for API <= 29 and AAudio (then
# OpenSL) from API 30. Builds test/android/opensl_fallback_test.cpp with the
# whole engine, replacing android_get_device_api_level() so each case sees
# the API level it sets, and runs it over adb: backend per level, real
# mixing, device stop/start, changeDevice, repeated init/deinit.
#
#   test/android/opensl_fallback_test.sh [--build-only] [--strict] [adb-serial]
#
# Platform: any host with the NDK; a device or emulator (adb) to run. Opens
# the real audio output (a quiet sine). Prints SKIPPED without the NDK, adb
# or a device; --build-only only builds. Env: see test/support/android_ndk.sh.
# Verification: run on a Pixel 8 API 34 arm64 emulator (macOS arm64 host, NDK 29).

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/android_engine_test.sh
# API >= 29, so the NDK declares android_get_device_api_level() instead of
# inlining it and the test's definition replaces it.
android_engine_test opensl_fallback_test 29 -- "$@"

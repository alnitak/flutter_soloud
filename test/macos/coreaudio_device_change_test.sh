#!/bin/bash
# CoreAudio output-device changes while the engine plays (macOS): builds
# test/macos/coreaudio_device_change_test.mm with the whole engine and runs
# it. Uses a process-private aggregate device (over BlackHole if installed,
# else over the default output) to remove the device in use and to switch
# devices with changeDevice, checking that the engine neither crashes nor
# stays silent and that Dart is told.
#
#   test/macos/coreaudio_device_change_test.sh [--strict]
#   SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT=1 test/macos/coreaudio_device_change_test.sh
#
# Platform: macOS with an output device (plays a quiet sine). By default the
# Mac's audio settings are not touched. With SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT=1
# it also switches the system default output for a few seconds and restores
# it afterwards, also on failure or a crash (not on SIGKILL).
# Verification: verified on macOS arm64 (Mac mini, BlackHole 16ch installed)
# without SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT; the opt-in default-output switch
# has not been run yet.

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIPPED: macOS only"
    exit 0
fi
. test/support/macos_engine_test.sh
macos_engine_test coreaudio_device_change_test "$@"

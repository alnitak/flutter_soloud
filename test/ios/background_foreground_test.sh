#!/bin/bash
# App background/foreground transitions on iOS with audio playing and paused:
# builds test/ios/background_foreground_test.mm with the whole engine for the
# iOS simulator and runs it with `xcrun simctl spawn`. Posts the UIApplication
# / UIScene lifecycle notifications and the suspension interruption in the
# orders iOS delivers them and checks the synthesized interruption_ended,
# the device restart, paused voices, voice-ended callbacks (none lost or
# duplicated) and the engine time.
#
#   test/ios/background_foreground_test.sh [--strict]
#
# Platform: macOS host with Xcode and an iOS simulator runtime (see
# test/support/ios_simulator.sh). Plays a quiet sine. Prints SKIPPED without
# the SDK, a runtime or a simulator. Simulated transitions: real suspension
# needs a device.
#   --strict  count KNOWN BUG checks as failures.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_engine_test.sh
ios_engine_test background_foreground_test "$@"

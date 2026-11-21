#!/bin/bash
# The engine leaves the app's AVAudioSession alone on iOS (sessionCategory =
# none, no activate/deactivate): builds test/ios/audio_session_category_test.mm
# with the whole engine for the iOS simulator and runs it with `xcrun simctl
# spawn`. Swizzles the AVAudioSession setters to record the engine's calls and
# checks category, mode and options after init, play, an interruption,
# stopAudioDevice and deinit, for two app configurations.
#
#   test/ios/audio_session_category_test.sh [--strict]
#
# Platform: macOS host with Xcode and an iOS simulator runtime (see
# test/support/ios_simulator.sh for picking/booting one). Plays a quiet sine.
# Prints SKIPPED without the SDK, a runtime or a simulator.
#   --strict  count KNOWN BUG checks as failures.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_engine_test.sh
ios_engine_test audio_session_category_test "$@"

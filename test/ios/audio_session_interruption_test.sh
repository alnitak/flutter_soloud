#!/bin/bash
# AVAudioSession interruptions and route changes on iOS: builds
# test/ios/audio_session_interruption_test.mm with the whole engine (the
# Objective-C++ miniaudio backend, soloud_miniaudio_objc.mm) for the iOS
# simulator and runs it with `xcrun simctl spawn`. Posts interruption
# began/ended (with and without shouldResume), route-change and media
# services reset notifications and checks the device stop/restart, the voices
# and the state events Dart receives.
#
#   test/ios/audio_session_interruption_test.sh [--strict]
#
# Platform: macOS host with Xcode and an iOS simulator runtime. Uses a booted
# simulator, or boots the first available iPhone and shuts it down afterwards
# (IOS_SIM_UDID picks one, see test/support/ios_simulator.sh). Plays a quiet
# sine through the simulator's (the Mac's) output. Prints SKIPPED without the
# SDK, a runtime or a simulator. The notifications are simulated: a real
# phone call or route change needs a device.
#   --strict  count KNOWN BUG checks as failures.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_engine_test.sh
ios_engine_test audio_session_interruption_test "$@"

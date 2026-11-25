#!/bin/bash
# AudioToolbox decoders on the iOS simulator: builds
# test/macos/aac_stream_decoder_apple_test.mm (the shared checks in
# test/support/os_decoder_checks.h plus Apple-only stream checks) for the
# simulator with os_decoder_apple.mm and aac_stream_decoder_apple.mm, and runs
# it with `xcrun simctl spawn`. The source lives in test/macos/ because the
# same file is the macOS test; nothing in it is macOS-only.
#
#   test/ios/aac_stream_decoder_apple_test.sh [--asan]
#
# Platform: macOS host with Xcode and an iOS simulator runtime. Uses a booted
# simulator, or boots the first available iPhone and shuts it down afterwards
# (IOS_SIM_UDID picks one, see test/support/ios_simulator.sh). Needs the
# fixtures in example/assets/audio; afconvert (host) makes the HE-AAC fixture.
# Prints SKIPPED without the SDK, a runtime or a simulator. No audio device.
#   --asan  build with AddressSanitizer and UBSan.
# Known src/ bugs are reported as KNOWN BUG and do not fail the run; set
# OSDEC_STRICT=1 to count them.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_simulator.sh

ios_sim_require || exit 0

OUT="${TMPDIR:-/tmp}/aac_stream_decoder_apple_test_iossim"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'ios_sim_release; rm -rf "$WORK_DIR"' EXIT

ios_sim_cxx -std=c++17 -O2 -Wall -fobjc-arc "${SANITIZER[@]}" \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/macos/aac_stream_decoder_apple_test.mm \
    src/native_decoder/os_decoder_apple.mm \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_apple.mm \
    src/audiobuffer/m4a_metadata.cpp \
    -framework AudioToolbox -framework CoreFoundation -framework Foundation

HE_AAC=""
if command -v afconvert > /dev/null; then
    HE_AAC="$WORK_DIR/he_aac.aac"
    afconvert -f adts -d aach example/assets/audio/sample-WAV.wav "$HE_AAC"
fi

ios_sim_acquire || exit 0
SIMCTL_CHILD_TMPDIR="$WORK_DIR" SIMCTL_CHILD_OSDEC_STRICT="${OSDEC_STRICT:-}" \
    ios_sim_run "$OUT" "$REPO_ROOT/example/assets/audio" "$WORK_DIR" "$HE_AAC"

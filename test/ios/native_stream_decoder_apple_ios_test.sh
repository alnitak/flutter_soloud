#!/bin/bash
# Streaming M4A/MP4 decoding on iOS (LoadMode.disk): builds
# test/ios/native_stream_decoder_apple_ios_test.cpp (the checks in
# test/support/native_stream_decoder_checks.h) for the iOS simulator with
# native_stream_decoder_apple.cpp, m4a_gapless.cpp and os_decoder_apple.mm,
# and runs it with `xcrun simctl spawn`. Covers AAC-LC, HE-AAC and HE-AAC v2
# priming/length/seek like test/macos/m4a_gapless_test.sh does on macOS, plus
# the bundled M4A assets, AC-3 in MP4 (if the OS decodes it), truncated and
# unsupported files and pinned descriptors.
#
#   test/ios/native_stream_decoder_apple_ios_test.sh [--asan] [--strict]
#
# Platform: macOS host with Xcode and an iOS simulator runtime. Uses a booted
# simulator, or boots the first available iPhone and shuts it down afterwards
# (IOS_SIM_UDID picks one, see test/support/ios_simulator.sh). The fixtures
# are made on the host (afconvert; ffmpeg optional, see
# test/support/native_stream_fixtures.sh). Prints SKIPPED without the SDK, a
# runtime, a simulator or afconvert. No audio device.
#   --asan    build with AddressSanitizer and UBSan.
#   --strict  count KNOWN BUG checks as failures.
# Env: CASE_FILTER=<regex> runs only the matching fixtures.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_simulator.sh
. test/support/native_stream_fixtures.sh

ios_sim_require || exit 0
if ! command -v afconvert > /dev/null; then
    echo "SKIPPED: afconvert not found"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/native_stream_decoder_apple_ios_test"
SANITIZER=()
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --strict) ARGS+=(--strict) ;;
    esac
done

WORK_DIR="$(mktemp -d)"
trap 'ios_sim_release; rm -rf "$WORK_DIR"' EXIT

ios_sim_cxx -std=c++17 -O2 -Wall "${SANITIZER[@]}" \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/ios/native_stream_decoder_apple_ios_test.cpp \
    src/native_stream/native_stream_decoder_apple.cpp \
    src/native_stream/m4a_gapless.cpp \
    src/native_decoder/os_decoder_apple.mm \
    -framework AudioToolbox -framework CoreFoundation

make_native_stream_fixtures "$WORK_DIR/fixtures" "$WORK_DIR"

ios_sim_acquire || exit 0
ios_sim_run "$OUT" "${ARGS[@]}" "$WORK_DIR/fixtures"

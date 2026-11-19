#!/bin/bash
# The common native decoder tests (test/common/native_decoder_test.cpp) built
# for the iOS simulator against the Apple decoders (os_decoder_apple.mm,
# aac_stream_decoder_apple.mm) and run with `xcrun simctl spawn`.
#
#   test/ios/native_decoder_test.sh [--asan]
#
# Platform: macOS host with Xcode, an iOS simulator runtime and afconvert.
# Uses a booted simulator, or boots the first available iPhone and shuts it
# down afterwards (IOS_SIM_UDID picks one, see test/support/ios_simulator.sh).
# Prints SKIPPED without the SDK, a runtime or a simulator. No audio device.
# The source shells out to afconvert, which iOS lacks: the AAC fixture is made
# here on the host and test/ios/ios_system_shim.h stands in for system()
# (and sets the working directory, see there).
#   --asan  build with AddressSanitizer and UBSan.
# Verification: verified on macOS arm64 with an iOS 26.1 simulator.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/ios_simulator.sh

ios_sim_require || exit 0
if ! command -v afconvert > /dev/null; then
    echo "SKIPPED: afconvert not found"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/native_decoder_test_iossim"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'ios_sim_release; rm -rf "$WORK_DIR"' EXIT

ios_sim_cxx -std=c++17 -O2 -Wall "${SANITIZER[@]}" \
    -include test/ios/ios_system_shim.h \
    -I src \
    -I src/soloud/include \
    -I src/native_decoder \
    -I src/audiobuffer \
    -I src/soloud/src/audiosource/wav \
    -o "$OUT" \
    test/common/native_decoder_test.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_apple.mm \
    src/native_decoder/os_decoder_apple.mm \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    -framework AudioToolbox -framework CoreFoundation

# testShortAacStreamDrains() converts a 0.25 s 440 Hz stereo tone with
# afconvert to $TMPDIR/native_decoder_short_aac.aac. Make the same file here:
# its first two packets must carry audio (silent packets are a few bytes,
# too few for AudioFileStream to report a format).
cat > "$WORK_DIR/tone.c" <<'TONE'
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    const uint32_t frames = 44100 / 4, rate = 44100, bytes = frames * 4;
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 1;
    uint32_t u32; uint16_t u16;
    fwrite("RIFF", 1, 4, f); u32 = 36 + bytes; fwrite(&u32, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); u32 = 16; fwrite(&u32, 4, 1, f);
    u16 = 1; fwrite(&u16, 2, 1, f); u16 = 2; fwrite(&u16, 2, 1, f);
    fwrite(&rate, 4, 1, f); u32 = rate * 4; fwrite(&u32, 4, 1, f);
    u16 = 4; fwrite(&u16, 2, 1, f); u16 = 16; fwrite(&u16, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < frames; ++i) {
        int16_t s = (int16_t)(8000.0 * sin(2.0 * M_PI * 440.0 * i / 44100.0));
        fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    }
    return fclose(f) != 0;
}
TONE
cc -O1 -o "$WORK_DIR/tone" "$WORK_DIR/tone.c" -lm
"$WORK_DIR/tone" "$WORK_DIR/tone.wav"
afconvert -f adts -d aac "$WORK_DIR/tone.wav" "$WORK_DIR/native_decoder_short_aac.aac"

ios_sim_acquire || exit 0
# The source opens example/assets/audio/... relative to the working
# directory, which simctl spawn leaves at /: the shim changes to
# SOLOUD_TEST_CWD before main().
SIMCTL_CHILD_TMPDIR="$WORK_DIR" SIMCTL_CHILD_SOLOUD_TEST_CWD="$REPO_ROOT" ios_sim_run "$OUT"

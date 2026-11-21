#!/bin/bash
# Build and run the standalone tests for the waveform reader in src/waveform/
# (test/common/waveform_test.cpp): Waveform::readSamples() point sampling and
# per-bar RMS on generated WAVs (float and 16-bit, mono and stereo), time
# ranges, clamping, buffer bounds, error codes, file vs memory input, and the
# repo assets in example/assets/audio/ (WAV, MP3, FLAC, OGG).
#
#   ./test/common/waveform_test.sh [--asan] [case]
#
# Device: none (only miniaudio's decoder is used). Assets:
# example/assets/audio/* (optional, missing ones are reported SKIPPED).
# When libogg/libvorbis are found (Homebrew or system prefix) the libvorbis
# backend (src/waveform/miniaudio_libvorbis.cpp) is built in and the OGG asset
# must decode; otherwise the test is built with -DNO_XIPH_LIBS and the OGG
# must report noBackend.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A case
# name (point, average, range, small, errors, file, assets, vorbis) runs only
# that case. Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/waveform_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

# libvorbis for the OGG backend, if present.
XIPH_FLAGS=(-DNO_XIPH_LIBS)
XIPH_LIBS=()
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/vorbis/vorbisfile.h" ] && [ -f "$prefix/include/ogg/ogg.h" ] &&
        printf 'int main() { return 0; }\n' |
            c++ -x c++ - -o /dev/null -L "$prefix/lib" -lvorbisfile -lvorbis -logg 2>/dev/null; then
        XIPH_FLAGS=(-I "$prefix/include")
        XIPH_LIBS=(-L "$prefix/lib" -lvorbisfile -lvorbis -logg)
        break
    fi
done
if [ "${XIPH_FLAGS[0]}" == "-DNO_XIPH_LIBS" ]; then
    echo "libvorbis not found: building without OGG support (NO_XIPH_LIBS)"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The miniaudio implementation on its own, rather than the SoLoud miniaudio
# backend (soloud_miniaudio.cpp), which pulls in the plugin's mixer output
# and analyzer.
printf '#define MINIAUDIO_IMPLEMENTATION\n#define MA_NO_MIDI\n#include "miniaudio.h"\n' |
    c++ -x c++ -std=c++17 -O2 -w -c "${SANITIZER[@]}" \
        -I src/soloud/src/backend/miniaudio \
        -o "$WORK_DIR/miniaudio_impl.o" -

PLATFORM_LIBS=()
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM_LIBS=(-framework CoreFoundation -framework CoreAudio -framework AudioToolbox)
else
    PLATFORM_LIBS=(-ldl -lm)
fi

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    "${XIPH_FLAGS[@]}" \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/waveform_test.cpp \
    src/waveform/waveform.cpp \
    src/waveform/miniaudio_libvorbis.cpp \
    src/soloud_common.cpp \
    "$WORK_DIR/miniaudio_impl.o" \
    "${XIPH_LIBS[@]}" \
    "${PLATFORM_LIBS[@]}"

"$OUT" "$@"

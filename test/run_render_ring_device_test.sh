#!/bin/bash
# Build and run the render-ahead ring smoke test on the real miniaudio
# backend. Needs an output device; skips cleanly on headless machines.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/render_ring_device_test"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The analyzer the device callback feeds uses pffft, which is C.
cc -std=gnu99 -O2 -c -I src/pffft -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

# The Wav source pulls in the decoders.
EXTRA_SRC=()
EXTRA_LDFLAGS=()
if [ "$(uname -s)" = "Darwin" ]; then
    EXTRA_SRC+=("src/native_decoder/os_decoder_apple.mm")
    EXTRA_SRC+=("src/audiobuffer/aac_stream_decoder_apple.mm")
    EXTRA_LDFLAGS+=("-framework" "AudioToolbox" "-framework" "CoreFoundation" "-framework" "Foundation")
else
    EXTRA_SRC+=("src/audiobuffer/aac_stream_decoder_linux.cpp")
fi

c++ -std=c++17 -O2 -Wall -Wextra -pthread ${EXTRA_CXXFLAGS:-} \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    test/render_ring_device_test.cpp \
    src/soloud_common.cpp \
    src/analyzer.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/soloud_biquadresonantfilter.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/native_decoder/*.cpp \
    "${EXTRA_SRC[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    "$WORK_DIR/pffft.o" \
    -ldl -lm \
    "${EXTRA_LDFLAGS[@]}"

"$OUT"

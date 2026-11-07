#!/bin/bash
# Build & run the standalone RealtimeRingBuffer, RealtimeStream, CustomAudioSource, and CustomFilter C++ tests.
# Run from the flutter_soloud repo root.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/realtime_and_custom_dsp_test"

# The Wav source (used by the checkpoint allocation test) pulls in the
# decoders.
EXTRA_SRC=()
EXTRA_LDFLAGS=()
if [ "$(uname -s)" = "Darwin" ]; then
    EXTRA_SRC+=("src/native_decoder/os_decoder_apple.mm")
    EXTRA_SRC+=("src/audiobuffer/aac_stream_decoder_apple.mm")
    EXTRA_LDFLAGS+=("-framework" "AudioToolbox" "-framework" "CoreFoundation" "-framework" "Foundation")
else
    EXTRA_SRC+=("src/audiobuffer/aac_stream_decoder_linux.cpp")
    EXTRA_LDFLAGS+=("-ldl")
fi

c++ -std=c++17 -O2 -Wall -pthread \
    -DWITH_NULL \
    -DNO_XIPH_LIBS \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -I src/soloud/include \
    -o "$OUT" \
    test/realtime_and_custom_dsp_test.cpp \
    src/audiobuffer/realtime_stream.cpp \
    src/filters/custom_filter.cpp \
    src/synth/custom_source.cpp \
    src/soloud/src/filter/soloud_echofilter.cpp \
    src/soloud/src/filter/soloud_biquadresonantfilter.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/native_decoder/*.cpp \
    "${EXTRA_SRC[@]}" \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LDFLAGS[@]}"

"$OUT"

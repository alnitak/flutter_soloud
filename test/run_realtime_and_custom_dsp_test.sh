#!/bin/bash
# Build & run the standalone RealtimeRingBuffer, RealtimeStream, CustomAudioSource, and CustomFilter C++ tests.
# Run from the flutter_soloud repo root.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/realtime_and_custom_dsp_test"

c++ -std=c++17 -O2 -Wall -pthread \
    -DWITH_NULL \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -o "$OUT" \
    test/realtime_and_custom_dsp_test.cpp \
    src/audiobuffer/realtime_stream.cpp \
    src/filters/custom_filter.cpp \
    src/synth/custom_source.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

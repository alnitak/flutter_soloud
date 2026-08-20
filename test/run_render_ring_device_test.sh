#!/bin/bash
# Build and run the render-ahead ring smoke test on the real miniaudio
# backend. Needs an output device; skips cleanly on headless machines.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/render_ring_device_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src \
    -o "$OUT" \
    test/render_ring_device_test.cpp \
    src/soloud_common.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    -ldl -lm

"$OUT"

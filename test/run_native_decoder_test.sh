#!/bin/bash
# Build & run the standalone native decoder correctness tests.
# Run from the flutter_soloud repo root.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/native_decoder_test"

c++ -std=c++17 -O2 -Wall \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -I src/soloud/src/audiosource/wav \
    -framework AudioToolbox \
    -framework CoreFoundation \
    -o "$OUT" \
    test/native_decoder_test.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_apple.mm \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp

"$OUT"

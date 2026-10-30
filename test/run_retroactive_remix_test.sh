#!/bin/bash
# Build and run the standalone native retroactive re-mix tests (Phase 3a of
# the retroactive re-mixing feature). Uses the null backend, so no audio
# hardware is needed.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/retroactive_remix_test"

EXTRA_SRC=()
EXTRA_LDFLAGS=()
if [ "$(uname -s)" = "Darwin" ]; then
    EXTRA_SRC+=("src/native_decoder/os_decoder_apple.mm")
    EXTRA_LDFLAGS+=("-framework" "AudioToolbox" "-framework" "CoreFoundation" "-framework" "Foundation")
fi

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL \
    -DNO_XIPH_LIBS \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -I src/soloud/include \
    -o "$OUT" \
    test/retroactive_remix_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/native_decoder/*.cpp \
    "${EXTRA_SRC[@]}" \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LDFLAGS[@]}"

"$OUT"

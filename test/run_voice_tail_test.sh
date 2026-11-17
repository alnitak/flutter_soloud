#!/bin/bash
# Build and run the standalone native voice tail tests: a voice that does not
# loop must play its last block to the end before the mixer stops it, so a
# copy scheduled at its end follows without a gap. Uses the null backend, so
# no audio hardware is needed.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/voice_tail_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/voice_tail_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

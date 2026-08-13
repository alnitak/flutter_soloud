#!/bin/bash
# Build and run the standalone native voice-ended callback regression tests.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/voice_ended_callback_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/voice_ended_callback_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

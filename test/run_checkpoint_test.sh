#!/bin/bash
# Build and run the standalone native mix-checkpoint tests (Phase 2 of the
# retroactive re-mixing feature): snapshot/restore of the full mixer state
# and the bit-exactness harness. Uses the null backend, so no audio hardware
# is needed.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/checkpoint_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/checkpoint_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

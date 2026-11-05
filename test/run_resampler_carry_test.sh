#!/bin/bash
# Build and run the standalone native resampler tests: every output sample
# must read the source where the play speed puts it, across the mixer's
# 512-sample source blocks. Uses the null backend, so no audio hardware is
# needed. -ffp-contract=off keeps the test's reference resampler and the
# engine's from fusing multiply-adds differently, so they compare bit for bit.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/resampler_carry_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread -ffp-contract=off \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/resampler_carry_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

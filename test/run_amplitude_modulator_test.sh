#!/bin/bash
# Build & run the standalone amplitude modulator DSP tests.
# Run from the flutter_soloud repo root.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/amplitude_modulator_test"

c++ -std=c++17 -O2 -Wall \
    -I src/soloud/include \
    -o "$OUT" \
    test/amplitude_modulator_test.cpp \
    src/filters/amplitude_modulator_filter.cpp \
    src/soloud/src/core/soloud_filter.cpp \
    src/soloud/src/core/soloud_fader.cpp

"$OUT"

#!/bin/bash
# Build and run the standalone native test for mixing bus visualization
# (Bus::getLevel, Bus::getWave, Bus::getApproximateVolume). Uses the null
# backend, so no audio hardware is needed.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/bus_visualization_test"

c++ -std=c++17 -O1 -g -Wall -pthread \
    -fsanitize=address -fno-omit-frame-pointer \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/bus_visualization_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

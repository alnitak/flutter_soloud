#!/bin/bash
# Build and run the standalone native tests for per-bus visualization: the
# SoLoud bus audio tap feeding an Analyzer. Built with AddressSanitizer, so a
# use-after-free of a bus instance or of an analyzer is reported
# deterministically.
#
# Pass --tsan to build under ThreadSanitizer instead.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-fsanitize=address -fno-omit-frame-pointer)
OUT="${TMPDIR:-/tmp}/bus_visualization_test"
if [ "$1" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread)
    OUT="${OUT}_tsan"
fi

cc -O1 -c "${SANITIZER[@]}" -o "$OUT-pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O1 -g -Wall -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -DSOLOUD_ANALYZER_TEST_HOOKS \
    -I src/soloud/include \
    -o "$OUT" \
    test/bus_visualization_test.cpp \
    src/analyzer.cpp \
    src/filters/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "$OUT-pffft.o"

"$OUT"

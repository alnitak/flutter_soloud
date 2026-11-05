#!/bin/bash
# Build and run the standalone native output-device swap regression tests.
#
# Unlike the other native tests here this one needs the miniaudio backend
# (that is where changeDevice lives) and therefore a real output device. On a
# machine without one the test reports SKIPPED and exits 0.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/change_device_test"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The analyzer the device callback feeds uses pffft, which is C.
cc -std=gnu99 -O2 -c -I src/pffft -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src \
    -o "$OUT" \
    test/change_device_test.cpp \
    src/soloud_common.cpp \
    src/analyzer.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    "$WORK_DIR/pffft.o" \
    -ldl -lm

"$OUT"

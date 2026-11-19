#!/bin/bash
# Build and run the unit tests of SoLoud::CircularFloatBuffer
# (test/common/circular_float_buffer_test.cpp): wrap-around, capacity 0/1,
# writes larger than the capacity, read/rewind ("peek") semantics, planar
# reads across the wrap boundary.
#
# Needs no audio device, no assets and no external libraries.
#
#   ./test/common/circular_float_buffer_test.sh [--asan]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/circular_float_buffer_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/circular_float_buffer_test.cpp \
    src/audiobuffer/circular_float_buffer.cpp

"$OUT" "$@"

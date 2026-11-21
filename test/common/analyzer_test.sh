#!/bin/bash
# Build and run the standalone tests for the visualization Analyzer
# (test/common/analyzer_test.cpp, src/analyzer.cpp + src/pffft): FFT peak bin
# of a known sine for every window size, normalized level vs
# min/max decibels, Blackman window leakage, wave window and channel merge,
# CHANNEL_ALL / selected channel, packet kinds, temporal smoothing, planar
# (bus tap) vs interleaved capture, invalid settings.
#
#   ./test/common/analyzer_test.sh [--asan] [case]
#
# Device: none (frames are fed straight into the analyzer; its worker thread
# runs for real). Assets: none.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A case
# name (peak, level, window, wave, channels, kinds, smoothing, planar,
# invalid) runs only that case. Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/analyzer_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99 (VLAs): build it with the C compiler.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

EXTRA_LIBS=()
if [ "$(uname)" != "Darwin" ]; then
    EXTRA_LIBS=(-ldl -lm)
fi

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/analyzer_test.cpp \
    src/analyzer.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

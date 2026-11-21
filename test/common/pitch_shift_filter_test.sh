#!/bin/bash
# Build and run the standalone tests for the plugin's PitchShift filter
# (test/common/pitch_shift_filter_test.cpp, src/filters/pitch_shift_filter.cpp,
# signalsmith-stretch): a sine's frequency moves by SHIFT / 2^(SEMITONES/12),
# length is preserved, wet mix, large/odd block sizes, 1..8 channels, live
# parameter changes. The filter instance is driven directly on synthetic
# buffers.
#
#   ./test/common/pitch_shift_filter_test.sh [--asan] [case]
#
# Device: none. Assets: none.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A case
# name (ratio, semitones, wet, blocks, params, probes) runs only that case.
# Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/pitch_shift_filter_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

EXTRA_LIBS=()
if [ "$(uname)" != "Darwin" ]; then
    EXTRA_LIBS=(-ldl -lm)
fi

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/pitch_shift_filter_test.cpp \
    src/filters/pitch_shift_filter.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

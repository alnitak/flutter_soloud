#!/bin/bash
# Build and run the standalone tests for the plugin's Compressor filter
# (test/common/compressor_test.cpp, src/filters/compressor.cpp): unity below
# threshold, makeup gain, static curve, soft knee, attack/release timing,
# wet mix, per-channel envelopes, parameter validation, sample-rate changes.
# The filter instance is driven directly on synthetic buffers.
#
#   ./test/common/compressor_test.sh [--asan] [case]
#
# Device: none. Assets: none.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A case
# name (unity, static, knee, attack, release, wet, channels, params,
# samplerate, sweep) runs only that case. Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/compressor_test"
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
    test/common/compressor_test.cpp \
    src/filters/compressor.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

#!/bin/bash
# Build and run the standalone smoke tests for the stock SoLoud filters the
# plugin exposes (test/common/soloud_filters_test.cpp): BiquadResonant, Echo,
# Lofi, Flanger, Bassboost, WaveShaper, Robotize, Freeverb. Each filter
# instance is driven directly on synthetic buffers.
#
#   ./test/common/soloud_filters_test.sh [--asan] [filter-name|probes]
#
# Device: none (no SoLoud engine is initialized). Assets: none.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# A filter name (biquad, echo, lofi, flanger, bassboost, waveshaper, robotize,
# freeverb) runs only that filter; "probes" runs only the informational
# edge-case probes. Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/soloud_filters_test"
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
    test/common/soloud_filters_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

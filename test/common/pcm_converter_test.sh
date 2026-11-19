#!/bin/bash
# Build and run the mixer-output PCM converter tests
# (test/common/pcm_converter_test.cpp): float -> F32LE/S8/S16LE/S32LE
# conversion, clamping/saturation, NaN handling, truncation, SIMD body vs
# scalar tail, misaligned output. No audio device or assets needed.
#
#   ./test/common/pcm_converter_test.sh [--asan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
# scenario name runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
fi

OUT="${TMPDIR:-/tmp}/pcm_converter_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/pcm_converter_test.cpp \
    src/mixeroutput/pcm_converter.cpp

"$OUT" "$@"

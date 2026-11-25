#!/bin/bash
# Build and run the standalone tests of src/simd/simd_pcm.h (see
# test/common/simd_pcm_test.cpp): every kernel with the host's SIMD backend
# against the same header built with -DDISABLE_SIMD
# (test/support/simd_pcm_scalar_ref.cpp) and a per-sample reference.
# Interleave / deinterleave (stereo, quad, N channels), int <-> float with
# clamp, reductions and clippers; lengths 0-33, unaligned pointers, NaN,
# +-Inf, values beyond +-1.
#
# Header-only: needs no audio device, no assets, no engine.
#
#   ./test/common/simd_pcm_test.sh [--asan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64 (NEON; plain and --asan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=simd_pcm_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The scalar build of the header, in its own translation unit.
c++ -std=c++17 -Wall -c "${SANITIZER[@]}" \
    -DDISABLE_SIMD \
    -I src \
    -o "$WORK_DIR/simd_pcm_scalar_ref.o" \
    test/support/simd_pcm_scalar_ref.cpp

c++ -std=c++17 -Wall -Wextra "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/simd_pcm_scalar_ref.o"

"$OUT" "$@"

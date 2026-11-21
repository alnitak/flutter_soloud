#!/bin/bash
# Build and run the FFT tests (test/common/fft_test.cpp): SoLoud's Ooura FFT
# (soloud_fft.cpp) and inverse against a reference DFT for 2..8192 points,
# tones, linearity, zeros, impulses; Soloud::calcFFT; PFFFT's ordered real
# transform used by the Analyzer and the parametric EQ.
#
# Device: none (null backend, mix() called directly). Assets: none.
#
#   ./test/common/fft_test.sh [--asan] [--strict] [case]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict makes the checks marked KNOWN BUG fail the run. A case name runs
# only that case. Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/fft_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

EXTRA_LIBS=()
if [ "$(uname)" != "Darwin" ]; then
    EXTRA_LIBS=(-ldl -lm)
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/fft_test.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

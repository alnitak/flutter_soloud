#!/bin/bash
# Build and run the standalone test of the SIMD layer's scalar fallback
# (test/simd_fallback_test.cpp) on the null backend.
#
# The test, the engine core, the analyzer and the filters are built twice:
# with the host's SIMD backend, and with -DDISABLE_SIMD, which selects the
# portable scalar code of src/simd/soloud_simd.h that targets without SSE2,
# NEON or WASM SIMD build. Both builds mix the same scene, and the scalar
# one must match what the SIMD one wrote.
#
# Pass --asan to build both with AddressSanitizer and
# UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/simd_fallback_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi
SCENE="$OUT.scene"

build() { # <binary> [extra flags...]
    local bin=$1
    shift
    cc -O2 "${SANITIZER[@]}" "$@" -c -o "$bin-pffft.o" src/pffft/pffft.c
    c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" "$@" \
        -DWITH_NULL \
        -I src \
        -I src/soloud/include \
        -o "$bin" \
        test/simd_fallback_test.cpp \
        src/analyzer.cpp \
        src/filters/*.cpp \
        src/soloud/src/core/*.cpp \
        src/soloud/src/filter/*.cpp \
        src/soloud/src/backend/null/soloud_null.cpp \
        "$bin-pffft.o"
}

echo "== host SIMD backend"
build "$OUT"
"$OUT" --dump "$SCENE"

echo "== -DDISABLE_SIMD"
build "${OUT}_scalar" -DDISABLE_SIMD
"${OUT}_scalar" --compare "$SCENE"

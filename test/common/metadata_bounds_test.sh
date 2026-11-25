#!/bin/bash
# Build and run the unit tests of metadataHasBytes()
# (test/common/metadata_bounds_test.cpp): offsets and lengths at the edges,
# lengths of UINT32_MAX / SIZE_MAX / UINT64_MAX, and the cases where
# `offset + length` wraps size_t.
#
# Needs no audio device and no assets.
#
#   ./test/common/metadata_bounds_test.sh [--asan|--wasm32|--m32]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --wasm32 builds with em++ and runs the result under node (32-bit size_t,
# as in the web build). Without em++ it prints SKIPPED. If em++'s own node is
# broken, point EM_NODE_JS at a working one.
# --m32 builds with `c++ -m32` (32-bit size_t, as on armeabi-v7a). Where the
# host compiler or its libraries cannot build 32-bit code (macOS arm64), it
# prints SKIPPED.
#
# Verified on macOS (arm64), also with --asan and --wasm32.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=metadata_bounds_test

if [ "${1:-}" == "--wasm32" ]; then
    shift
    if ! command -v em++ >/dev/null 2>&1; then
        echo "SKIPPED: em++ not found (needed for --wasm32)"
        exit 0
    fi
    OUT="${TMPDIR:-/tmp}/${NAME}_wasm.js"
    em++ -std=c++17 -O2 -w -I src -s EXIT_RUNTIME=1 -o "$OUT" test/common/$NAME.cpp
    "${EM_NODE_JS:-node}" "$OUT" "$@"
    exit $?
fi

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--m32" ]; then
    SANITIZER=(-O2 -m32)
    OUT="${OUT}_m32"
    shift
    if ! c++ -std=c++17 -w "${SANITIZER[@]}" -I src -o "$OUT" test/common/$NAME.cpp 2>/dev/null; then
        echo "SKIPPED: this compiler cannot build 32-bit (-m32) executables"
        exit 0
    fi
    "$OUT" "$@"
    exit $?
fi

c++ -std=c++17 -w "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp

export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$OUT" "$@"

#!/bin/bash
# Build and run the unit tests of the M4A / MP4 metadata box parser
# (test/common/m4a_metadata_test.cpp, src/audiobuffer/m4a_metadata.cpp):
# every mapped iTunes item, the sample entry and esds bitrate, the bundled
# M4A/MP4 assets, 64-bit and to-the-end box sizes, deep nesting on a 1 MiB
# stack, sizes that wrap, every truncated prefix.
#
# Needs no audio device and no libraries. Reads the M4A/MP4 files in
# example/assets/audio (that case is skipped without them).
#
#   ./test/common/m4a_metadata_test.sh [--asan|--wasm32] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --wasm32 builds with em++ and runs the result under node, so the parser
# runs with a 32-bit size_t and a WebAssembly stack, as in the web build
# (and, for size_t, on armeabi-v7a): the wrap case can only fail there. If
# em++'s own node is broken, point EM_NODE_JS at a working one. Without em++
# it prints SKIPPED.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS (arm64), also with --asan and --wasm32.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=m4a_metadata_test

if [ "${1:-}" == "--wasm32" ]; then
    shift
    if ! command -v em++ >/dev/null 2>&1; then
        echo "SKIPPED: em++ not found (needed for --wasm32)"
        exit 0
    fi
    OUT="${TMPDIR:-/tmp}/${NAME}_wasm.js"
    # Memory and stack settings follow web/compile_wasm.sh.
    em++ -std=c++17 -O2 -w -pthread -I src \
        -s STACK_SIZE=4194304 -s ALLOW_MEMORY_GROWTH=1 \
        -s INITIAL_MEMORY=67108864 -s MAXIMUM_MEMORY=2147483648 \
        -s PTHREAD_POOL_SIZE=2 -s EXIT_RUNTIME=1 -s NODERAWFS=1 \
        -o "$OUT" test/common/$NAME.cpp src/audiobuffer/m4a_metadata.cpp
    "${EM_NODE_JS:-node}" "$OUT" example/assets/audio "$@"
    exit $?
fi
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/audiobuffer/m4a_metadata.cpp

export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$OUT" example/assets/audio "$@"

#!/bin/bash
# Build and run the standalone native test for the ownership of the buffer
# passed to loadMem() (see test/common/load_mem_ownership_test.cpp).
#
# Like the lifecycle tests this links the plugin's own `bindings.cpp`, so
# loadMem() is called through the export Dart uses, on a real engine.
# The build enables only miniaudio's null backend, so it needs no audio
# device. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines, and
# without the Xiph libraries.
#
# Pass --asan to build under AddressSanitizer: a read of the buffer after it
# was freed, or a second free of it, then aborts with a report. The formats
# SoLoud parses also run with alloc_dealloc_mismatch enabled (it is off by
# default on macOS), so a delete[] of the caller's calloc'd buffer aborts too;
# the ones that fall back to the platform decoder run without it, because
# Apple's AAC decoder has a mismatch of its own.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/load_mem_ownership_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here rather
# than letting the C++ front end reject its VLAs.
cc -std=gnu99 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The plugin's sources, shared by the native test builds
# (test/support/plugin_sources.txt).
. test/support/engine_sources.sh
plugin_sources

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DNO_XIPH_LIBS \
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/load_mem_ownership_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

if [ "$1" == "--asan" ]; then
    ASAN_OPTIONS="alloc_dealloc_mismatch=1:${ASAN_OPTIONS:-}" "$OUT" soloud
    ASAN_OPTIONS="alloc_dealloc_mismatch=0:${ASAN_OPTIONS:-}" "$OUT" platform
else
    "$OUT"
fi

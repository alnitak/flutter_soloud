#!/bin/bash
# Build and run the standalone native test for playing a sound streamed from
# disk whose file has been deleted since it was loaded (see
# test/common/play_missing_file_test.cpp).
#
# Like the lifecycle tests this links the plugin's own `bindings.cpp` -- the
# engine is driven through the same exports Dart uses -- so it needs the whole
# plugin translation unit set. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines, for
# the sound-count export.
#
# The test needs an engine that can initialize. The build enables only
# miniaudio's null backend, so it needs no audio device; a failure to
# initialize is treated as a broken environment rather than a reason to pass
# vacuously (set SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE=1 to downgrade that to
# a skip).
#
# Pass --asan to build under AddressSanitizer and UndefinedBehaviorSanitizer,
# or --tsan to build under ThreadSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/play_missing_file_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "$1" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
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
    test/common/play_missing_file_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT" "$@"

#!/bin/bash
# Build and run the standalone native device-lifecycle coordinator tests.
#
# Unlike the other native tests here this one links the plugin's own
# `bindings.cpp` -- the lifecycle ownership lives there -- so it needs the whole
# plugin translation unit set. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines.
#
# Every scenario needs an engine that can initialize. miniaudio falls back to its
# null backend on a machine with no audio hardware, so a failure to open one is
# treated as a broken environment rather than a reason to pass vacuously; set
# SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE=1 to downgrade that to a skip.
#
# Pass --tsan to build under ThreadSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/device_coordinator_test"
if [ "$1" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here rather
# than letting the C++ front end reject its VLAs.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The plugin's sources, shared by the native test builds
# (test/support/plugin_sources.txt).
. test/support/engine_sources.sh
plugin_sources

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/device_coordinator_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT"

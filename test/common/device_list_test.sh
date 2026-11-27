#!/bin/bash
# Build and run the standalone native playback-device list regression tests.
#
# Like the engine lifecycle test, this links the plugin's own `bindings.cpp`,
# where the device list exports live, and so the whole plugin translation unit
# set. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines: one
# of them replaces the backend's devices with a list the test chooses, so no
# audio device is needed.
#
# Pass --asan to build under AddressSanitizer (a write past the caller's
# arrays is reported there), or --tsan to build under ThreadSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/device_list_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
elif [ "$1" == "--tsan" ]; then
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
    test/common/device_list_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT"

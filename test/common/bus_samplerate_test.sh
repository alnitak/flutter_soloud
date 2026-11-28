#!/bin/bash
# Build and run the standalone native tests for the rate a mixing bus is mixed
# at (test/common/bus_samplerate_test.cpp).
#
#   ./test/common/bus_samplerate_test.sh [--asan|--tsan] [scenario]
#
# It links the plugin's bindings and Player, so it needs the whole plugin
# translation unit set, like test/common/filter_params_test.sh, plus SoLoud's null
# driver for the scenario that mixes by hand. The "player" scenario goes
# through initEngine(); the build enables only miniaudio's null backend, so
# it needs no audio device. --asan builds with AddressSanitizer and
# UndefinedBehaviorSanitizer, --tsan with ThreadSanitizer. A scenario name runs
# only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/bus_samplerate_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
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
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DWITH_NULL \
    -DNO_XIPH_LIBS \
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/bus_samplerate_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT" "$@"

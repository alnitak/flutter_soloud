#!/bin/bash
# Build and run the robustness test of every function src/bindings.cpp
# exports to Dart (see test/common/bindings_null_args_test.cpp): each one is
# called before initEngine(), with the engine running and after dispose(),
# with handle 0, made-up handles (0x0BAD0BAD, 0xFFFFFFFF), unknown sound
# hashes and bus ids, null pointers, zero / negative lengths, out-of-range
# enums and NaN. No call may crash or hang, and a PlayerErrors function must
# return an error code for an invalid input. Every (function, phase) runs in
# its own forked child, so a crash is attributed to the call that caused it.
#
# Links the plugin's own bindings.cpp and plays through miniaudio's null
# backend: no audio device, no assets. Built without the Xiph libraries
# (NO_XIPH_LIBS) and without VST3, so the VST3 exports are their stubs.
#
#   ./test/common/bindings_null_args_test.sh [--asan] [--strict] [filter]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
# [filter] runs only the cases whose name contains it.
#
# Verified on macOS arm64 (plain and --asan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=bindings_null_args_test
SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
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
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

# Children abort on the first sanitizer report; the parent attributes it.
export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=0:detect_leaks=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$OUT" "$@"

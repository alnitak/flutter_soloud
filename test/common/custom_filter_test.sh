#!/bin/bash
# Build and run the standalone tests of SoLoud::CustomFilter
# (src/filters/custom_filter.cpp) through the plugin's Filters class
# (src/filters/filters.cpp), see test/common/custom_filter_test.cpp: 8 slots
# per scope (global, bus, sound) and their execution order, bypass, wet/dry,
# parameter fade and oscillation, the callback replaced while the mixer runs,
# a slot removed from the API thread during the mix.
#
# Needs no audio device and no assets: SoLoud's null driver, mixed by the
# test (on its own thread for the threaded cases).
#
#   ./test/common/custom_filter_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=custom_filter_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
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

# pffft.c is C99 (the analyzer the bus data owns needs it).
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src -I src/soloud/include \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/synth/custom_source.cpp \
    src/analyzer.cpp \
    src/filters/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "$WORK_DIR/pffft.o"

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

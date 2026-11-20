#!/bin/bash
# Build and run the fader operation tests (test/common/fader_ops_test.cpp):
# volume, pan, speed and global volume fades and oscillations
# (soloud_core_faderops.cpp with the local patches), a fade interrupted by
# another, schedulePause/scheduleStop during a fade, durations 0 and
# negative.
#
# Device: none (null backend, mix() called directly). Assets: none.
#
#   ./test/common/fader_ops_test.sh [--asan] [--strict] [case]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict makes the checks marked KNOWN BUG fail the run. A case name runs
# only that case. Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/fader_ops_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

EXTRA_LIBS=()
if [ "$(uname)" != "Darwin" ]; then
    EXTRA_LIBS=(-ldl -lm)
fi

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/fader_ops_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

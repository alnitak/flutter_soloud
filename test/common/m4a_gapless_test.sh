#!/bin/bash
# Build and run the robustness tests of the gapless-info parser of the native
# streams (test/common/m4a_gapless_test.cpp, src/native_stream/m4a_gapless.cpp)
# on synthetic MP4 files: edit lists, iTunSMPB, box sizes 0, 1 and past the
# file, deep nesting, sample tables that disagree, truncated files.
#
# Needs no audio device, no assets and no libraries.
#
#   ./test/common/m4a_gapless_test.sh [--asan]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
#
# Verified on macOS (arm64), also with --asan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=m4a_gapless_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/native_stream/m4a_gapless.cpp

export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$OUT" "$@"

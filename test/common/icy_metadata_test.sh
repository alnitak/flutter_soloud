#!/bin/bash
# Build and run the unit tests of the ICY (Shoutcast) metadata stripper
# (test/common/icy_metadata_test.cpp): audio bytes given back intact and one
# callback per title change, with metadata blocks split anywhere across
# chunks, zero-length blocks, blocks longer than the data, unterminated
# titles and metaint 0 / 1.
#
# Needs no audio device and no assets.
#
#   ./test/common/icy_metadata_test.sh [--asan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS (arm64), also with --asan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=icy_metadata_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -w "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp

export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
"$OUT" "$@"

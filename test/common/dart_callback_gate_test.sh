#!/bin/bash
# Build and run the standalone tests of src/dart_callback_gate.h (see
# test/common/dart_callback_gate_test.cpp): claim / retire / isOwnedBy,
# generations across a simulated hot restart, retirement waiting for
# in-flight invocations and blocking new ones, claims racing invocations.
#
# Header-only: needs no audio device, no assets, no engine.
#
#   ./test/common/dart_callback_gate_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=dart_callback_gate_test
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

c++ -std=c++17 -Wall -Wextra -pthread "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/$NAME.cpp

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

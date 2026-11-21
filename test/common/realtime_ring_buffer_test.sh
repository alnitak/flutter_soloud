#!/bin/bash
# Build and run the standalone tests of SoLoud::RealtimeRingBuffer
# (src/audiobuffer/realtime_ring_buffer.h, see
# test/common/realtime_ring_buffer_test.cpp): capacity and history layout,
# wraps with 1-8 channels and all push formats, full ring, history rewind
# with setReadFrame(), one producer + one consumer thread with numbered
# frames, reset() during use.
#
# Header-only: needs no audio device, no assets, no engine.
#
#   ./test/common/realtime_ring_buffer_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=realtime_ring_buffer_test
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
    -I src -I src/soloud/include \
    -o "$OUT" \
    test/common/$NAME.cpp

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

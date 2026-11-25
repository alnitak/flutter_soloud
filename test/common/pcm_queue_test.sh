#!/bin/bash
# Build and run the mixer-output PCM chunk queue tests
# (test/common/pcm_queue_test.cpp): FIFO order, unbounded growth, blocking
# pop on underflow, stop() semantics, and producer/consumer concurrency.
# No audio device or assets needed.
#
#   ./test/common/pcm_queue_test.sh [--asan|--tsan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. A scenario name runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g)
    SUFFIX="_tsan"
    shift
fi

OUT="${TMPDIR:-/tmp}/pcm_queue_test$SUFFIX"

c++ -std=c++17 -O1 -w -pthread "${SANITIZER[@]}" \
    -I src \
    -o "$OUT" \
    test/common/pcm_queue_test.cpp \
    src/mixeroutput/pcm_queue.cpp

"$OUT" "$@"

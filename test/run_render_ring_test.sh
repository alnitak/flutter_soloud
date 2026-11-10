#!/bin/bash
# Build and run the standalone native render-ahead ring tests (Phase 1 of the
# retroactive re-mixing feature). Uses the null backend, so no audio hardware
# is needed.
#
# Pass --tsan to build under ThreadSanitizer, or --asan for AddressSanitizer
# with UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/render_ring_test"
if [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
elif [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

c++ -std=c++17 -Wall -Wextra -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/render_ring_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

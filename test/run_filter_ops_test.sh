#!/bin/bash
# Build and run the standalone native tests for filter instances and the core
# filter operations (test/filter_ops_test.cpp) on the null backend.
#
#   ./test/run_filter_ops_test.sh [--asan|--tsan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer (for the `getter` race). A scenario name runs only
# that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/filter_ops_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

cc -O2 "${SANITIZER[@]}" -c -o "$OUT-pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/filter_ops_test.cpp \
    src/filters/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "$OUT-pffft.o"

"$OUT" "$@"

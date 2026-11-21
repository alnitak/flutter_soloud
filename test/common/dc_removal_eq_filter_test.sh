#!/bin/bash
# Build and run the DCRemovalFilter / EqFilter tests
# (test/common/dc_removal_eq_filter_test.cpp): measured DC removal against
# the moving-average model, length/wet/channels/rate, long-run drift, EQ
# transparency at unity gains, EQ parameter limits and finite output, and
# the FFTFilter framing as used by the plugin's Bassboost filter.
#
# Device: none (filter instances driven directly). Assets: none.
#
#   ./test/common/dc_removal_eq_filter_test.sh [--asan] [--strict] [case]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict makes the checks marked KNOWN BUG fail the run. A case name runs
# only that case. Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/dc_removal_eq_filter_test"
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
    test/common/dc_removal_eq_filter_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

"$OUT" "$@"

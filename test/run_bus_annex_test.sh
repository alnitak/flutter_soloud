#!/bin/bash
# Build and run the standalone native tests for annexing a voice into a mixing
# bus that is not playing. Built with AddressSanitizer, so annexing into a bus
# that was never played (which used to dereference a null engine) is reported
# deterministically.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/bus_annex_test"

c++ -std=c++17 -O1 -g -Wall -pthread \
    -fsanitize=address -fno-omit-frame-pointer \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/bus_annex_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

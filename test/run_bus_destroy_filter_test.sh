#!/bin/bash
# Build and run the standalone native regression test for destroying a
# playing mixing bus with filters attached. Built with AddressSanitizer so a
# use-after-free of the bus filters is reported deterministically.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/bus_destroy_filter_test"

cc -O1 -c -o "$OUT-pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O1 -g -Wall -pthread \
    -fsanitize=address -fno-omit-frame-pointer \
    -DWITH_NULL \
    -I src/soloud/include \
    -o "$OUT" \
    test/bus_destroy_filter_test.cpp \
    src/filters/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "$OUT-pffft.o"

"$OUT"

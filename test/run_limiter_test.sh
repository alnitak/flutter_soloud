#!/bin/bash
# Build & run the standalone limiter correctness tests.
# Run from the flutter_soloud repo root.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/limiter_test"

c++ -std=c++17 -O2 -Wall \
    -I src/soloud/include \
    -o "$OUT" \
    test/limiter_test.cpp src/filters/limiter.cpp

"$OUT"

#!/bin/bash
# Build and run the Ogg Opus capture encoder tests
# (test/opus_output_encoder_test.cpp). They need libopus and libogg: set
# XIPH_PREFIX to where they are installed (default /usr/local).
#
#   ./test/run_opus_output_encoder_test.sh [--asan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
# scenario name runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

XIPH="${XIPH_PREFIX:-/usr/local}"

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
fi

OUT="${TMPDIR:-/tmp}/opus_output_encoder_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
    -I src \
    -I "$XIPH/include" \
    -o "$OUT" \
    test/opus_output_encoder_test.cpp \
    -L "$XIPH/lib" -lopus -logg

"$OUT" "$@"

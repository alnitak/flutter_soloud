#!/bin/bash
# Build and run the mixer-output capture ring tests (test/mixer_output_test.cpp).
# They drive MixerOutput directly, so no engine or audio device is needed.
#
#   ./test/run_mixer_output_test.sh [--asan|--tsan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. A scenario name runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
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

OUT="${TMPDIR:-/tmp}/mixer_output_test$SUFFIX"

c++ -std=c++17 -O1 -w -pthread "${SANITIZER[@]}" \
    -DNO_XIPH_LIBS \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/mixer_output_test.cpp \
    src/mixeroutput/*.cpp

"$OUT" "$@"

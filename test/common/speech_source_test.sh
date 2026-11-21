#!/bin/bash
# Build and run the standalone tests for SoLoud's Speech audio source
# (test/common/speech_source_test.cpp, src/soloud/src/audiosource/speech),
# the source behind the plugin's textToSpeech(): non-silent finite output for
# a sentence, duration vs text length, replay determinism, base waveform /
# frequency / speed, empty and odd text (punctuation, digits, non-ASCII,
# control characters), very long text (~100k characters), setText() while
# playing.
#
#   ./test/common/speech_source_test.sh [--asan] [case]
#
# Device: none (SoLoud null driver, mixed by hand). Assets: none.
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer (any
# UBSan diagnostic aborts the run with a non-zero exit). A case
# name (basic, duration, replay, params, empty, long, retarget) runs only
# that case. Works on macOS and Linux.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/speech_source_test"
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
    test/common/speech_source_test.cpp \
    src/soloud/src/audiosource/speech/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${EXTRA_LIBS[@]}"

if [ ${#SANITIZER[@]} -gt 0 ]; then
    # Make any UBSan diagnostic (e.g. signed overflow, bug O3) fail the run.
    export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1${UBSAN_OPTIONS:+:$UBSAN_OPTIONS}"
fi

"$OUT" "$@"

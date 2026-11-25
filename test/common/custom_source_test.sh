#!/bin/bash
# Build and run the standalone tests of SoLoud::CustomAudioSource
# (src/synth/custom_source.cpp, see test/common/custom_source_test.cpp):
# callback arguments and lifecycle events, short writes and over-reports,
# end of stream, 1-8 channels, several voices and single instance, the
# source destroyed while it plays (also from another thread while mix()
# runs), playing through a bus and in 3D.
#
# Needs no audio device and no assets: SoLoud's null driver, mixed by the
# test.
#
#   ./test/common/custom_source_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=custom_source_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src -I src/soloud/include \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/synth/custom_source.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

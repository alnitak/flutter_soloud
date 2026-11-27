#!/bin/bash
# Build and run the standalone concurrency test of Player (src/player.cpp)
# through the exports Dart uses (src/bindings.cpp), see
# test/common/player_concurrency_test.cpp: loader threads loading the same
# and new sounds, the UI thread playing / stopping / disposing them, a
# realtime feeder, a samples reader and the voice-ended / file-loaded
# callbacks, while the engine is torn down and re-initialized repeatedly.
#
# Needs no audio device and no assets: miniaudio's null backend only
# (MA_ENABLE_ONLY_SPECIFIC_BACKENDS + MA_ENABLE_NULL). No Xiph libraries.
#
#   ./test/common/player_concurrency_test.sh [--asan|--tsan] [--strict]
#       [--api-threads N] [--cycles N]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --api-threads (default 1, as Dart has one UI
# isolate) runs more threads calling the play/stop/dispose exports at once.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=player_concurrency_test
SANITIZER=(-O1 -g)
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

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The plugin's sources, shared by the native test builds
# (test/support/plugin_sources.txt).
. test/support/engine_sources.sh
plugin_sources

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DNO_XIPH_LIBS \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1 second_deadlock_stack=1}"
fi
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
# The decoders print a line per stream load, and loadFile() warns when the
# engine is down (which loads racing a deinit expect): drop those.
set +e
"$OUT" "$@" 2>&1 | grep -v -e '^\[WavStream::parse\]' \
    -e '^WARNING (from SoLoud C++ binding code): the player has not yet been initialized'
STATUS=${PIPESTATUS[0]}
exit "$STATUS"

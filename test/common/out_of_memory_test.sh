#!/bin/bash
# Build and run the out-of-memory tests (see
# test/common/out_of_memory_test.cpp): operator new is replaced by a
# countdown allocator (test/support/out_of_memory_allocator.h) that fails the
# N-th allocation of an operation, for every N, while
#   - SoLoud::Wav loads WAV / MP3 / Ogg Vorbis / FLAC from memory,
#   - SoLoud::WavStream loads and starts a voice,
#   - Soloud::play / play3d / Bus voices are created (with and without a
#     filter on the sound),
#   - the stream decoders are fed,
#   - the FFI exports loadMem, play, play3d, loadWaveform, setBufferStream +
#     addAudioDataStream, createBus + busPlayOnEngine run.
# Checks: no crash or deadlock (each scenario runs in a child process), an
# error or a complete success, the object usable again, no leaked
# allocation, no C++ exception escaping an FFI export. malloc failures inside
# dr_libs / miniaudio are not injected (operator new only).
#
# Needs no audio device (SoLoud's null driver and miniaudio's null backend).
# Reads example/assets/audio (or SOLOUD_TEST_ASSETS); missing files are
# skipped. Links the whole plugin (bindings.cpp) without the Xiph libraries.
#
#   ./test/common/out_of_memory_test.sh [--asan] [--strict] [scenario]
#   scenarios: wav wavstream voices voices_filter stream_decoders bindings
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain and --asan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=out_of_memory_test
SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here rather
# than letting the C++ front end reject its VLAs.
cc -std=gnu99 -O2 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The plugin's sources, shared by the native test builds
# (test/support/plugin_sources.txt).
. test/support/engine_sources.sh
plugin_sources

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO -DWITH_NULL \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DNO_XIPH_LIBS \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT" "$@"

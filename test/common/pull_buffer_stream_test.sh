#!/bin/bash
# Build and run the direct tests of the pull stream
# (test/common/pull_buffer_stream_test.cpp): short and empty deliveries, data
# ending mid-frame, seeks past the end and between frames, reset and reuse,
# delivery/reset/read on several threads, on raw PCM made in memory.
#
# It links the plugin's own `bindings.cpp`, so the stream is created through
# the exports Dart uses, and it needs the Xiph libraries the plugin links
# (brew install opus libogg libvorbis flac, or the distribution's -dev
# packages); without them the test is skipped. It runs an engine on
# miniaudio's null backend: no audio device, no assets.
#
#   ./test/common/pull_buffer_stream_test.sh [--asan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=pull_buffer_stream_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

XIPH_FLAGS=()
XIPH_LIBS=()
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/vorbis/vorbisfile.h" ] &&
       [ -f "$prefix/include/FLAC/stream_decoder.h" ] && [ -f "$prefix/include/ogg/ogg.h" ]; then
        XIPH_FLAGS=(-I "$prefix/include")
        XIPH_LIBS=(-L "$prefix/lib" -lvorbisfile -lvorbisenc -lvorbis -logg -lopus -lFLAC)
        break
    fi
done
if [ ${#XIPH_LIBS[@]} -eq 0 ]; then
    echo "SKIPPED: Xiph libraries not found (brew install opus libogg libvorbis flac)"
    exit 0
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it.
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
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    "${XIPH_FLAGS[@]}" \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${XIPH_LIBS[@]}" \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

"$OUT" "$@"

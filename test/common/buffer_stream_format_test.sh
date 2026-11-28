#!/bin/bash
# Build and run the standalone native test for an AUTO buffer stream's format
# (see test/common/buffer_stream_format_test.cpp): a voice must adopt the format of a
# new source delivered after a reset, and keep its relative play speed when it
# adopts one.
#
# It links the plugin's own `bindings.cpp`, so the stream is driven through
# the exports Dart uses. It needs the Xiph libraries (brew install opus libogg
# libvorbis flac, or the distribution's -dev packages). It mixes a real engine
# on miniaudio's null backend by hand, so no audio hardware is used.
#
#   ./test/common/buffer_stream_format_test.sh [--asan|--tsan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/buffer_stream_format_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

XIPH_FLAGS=()
XIPH_LIBS=()
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/vorbis/vorbisfile.h" ]; then
        XIPH_FLAGS=(-I "$prefix/include")
        XIPH_LIBS=(-L "$prefix/lib" -lvorbisfile -lvorbisenc -lvorbis -logg -lopus -lFLAC)
        break
    fi
done
if [ ${#XIPH_LIBS[@]} -eq 0 ]; then
    echo "Xiph libraries not found (brew install opus libogg libvorbis flac)" >&2
    exit 2
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

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    "${XIPH_FLAGS[@]}" \
    "${PLUGIN_INCLUDES[@]}" \
    -o "$OUT" \
    test/common/buffer_stream_format_test.cpp \
    "$WORK_DIR/pffft.o" \
    "${PLUGIN_SOURCES[@]}" \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${XIPH_LIBS[@]}" \
    "${PLUGIN_LIBS[@]}" \
    -ldl -lm

if [ -n "${SANITIZER[*]}" ] && [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=0 second_deadlock_stack=1}"
fi
"$OUT" "$@"

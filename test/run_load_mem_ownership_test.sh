#!/bin/bash
# Build and run the standalone native test for the ownership of the buffer
# passed to loadMem() (see test/load_mem_ownership_test.cpp).
#
# Like the lifecycle tests this links the plugin's own `bindings.cpp`, so
# loadMem() is called through the export Dart uses, on a real engine;
# miniaudio falls back to its null backend on a machine with no audio
# hardware. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines, and
# without the Xiph libraries.
#
# Pass --asan to build under AddressSanitizer: a read of the buffer after it
# was freed, or a second free of it, then aborts with a report. The formats
# SoLoud parses also run with alloc_dealloc_mismatch enabled (it is off by
# default on macOS), so a delete[] of the caller's calloc'd buffer aborts too;
# the ones that fall back to the platform decoder run without it, because
# Apple's AAC decoder has a mismatch of its own.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/load_mem_ownership_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# pffft.c is C99: the CMake build forces C for it, so do the same here rather
# than letting the C++ front end reject its VLAs.
cc -std=gnu99 -c "${SANITIZER[@]}" \
    -I src/pffft \
    -o "$WORK_DIR/pffft.o" \
    src/pffft/pffft.c

# The OS decoder backends are compiled per platform, like src/CMakeLists.txt.
PLATFORM_SOURCES=()
PLATFORM_LIBS=()
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM_SOURCES=(
        src/native_decoder/os_decoder_apple.mm
        src/audiobuffer/aac_stream_decoder_apple.mm
    )
    PLATFORM_LIBS=(-framework AudioToolbox -framework CoreFoundation)
else
    PLATFORM_SOURCES=(src/native_decoder/os_decoder_linux.cpp)
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    -I src/soloud/include \
    -I src/soloud/src \
    -I src/pffft \
    -I src \
    -I src/native_decoder \
    -o "$OUT" \
    test/load_mem_ownership_test.cpp \
    "$WORK_DIR/pffft.o" \
    src/soloud_common.cpp \
    src/device_lifecycle_test_hooks.cpp \
    src/bindings.cpp \
    src/player.cpp \
    src/analyzer.cpp \
    src/synth/*.cpp \
    src/waveform/waveform.cpp \
    src/waveform/miniaudio_libvorbis.cpp \
    src/audiobuffer/*.cpp \
    src/native_stream/*.cpp \
    src/native_decoder/native_audio_decoder.cpp \
    "${PLATFORM_SOURCES[@]}" \
    src/filters/*.cpp \
    src/mixeroutput/*.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/filter/*.cpp \
    src/soloud/src/audiosource/ay/*.cpp \
    src/soloud/src/audiosource/monotone/*.cpp \
    src/soloud/src/audiosource/noise/*.cpp \
    src/soloud/src/audiosource/sfxr/*.cpp \
    src/soloud/src/audiosource/speech/*.cpp \
    src/soloud/src/audiosource/tedsid/*.cpp \
    src/soloud/src/audiosource/vic/*.cpp \
    src/soloud/src/audiosource/vizsn/*.cpp \
    src/soloud/src/audiosource/wav/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    "${PLATFORM_LIBS[@]}" \
    -ldl -lm

if [ "$1" == "--asan" ]; then
    ASAN_OPTIONS="alloc_dealloc_mismatch=1:${ASAN_OPTIONS:-}" "$OUT" soloud
    ASAN_OPTIONS="alloc_dealloc_mismatch=0:${ASAN_OPTIONS:-}" "$OUT" platform
else
    "$OUT"
fi

#!/bin/bash
# Build and run the standalone native test for playing a sound streamed from
# disk whose file has been deleted since it was loaded (see
# test/play_missing_file_test.cpp).
#
# Like the lifecycle tests this links the plugin's own `bindings.cpp` -- the
# engine is driven through the same exports Dart uses -- so it needs the whole
# plugin translation unit set. It is built with the test-only hooks enabled
# (SOLOUD_LIFECYCLE_TEST_HOOKS), which the shipping build never defines, for
# the sound-count export.
#
# The test needs an engine that can initialize. miniaudio falls back to its
# null backend on a machine with no audio hardware, so a failure to open one
# is treated as a broken environment rather than a reason to pass vacuously;
# set SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE=1 to downgrade that to a skip.
#
# Pass --asan to build under AddressSanitizer and UndefinedBehaviorSanitizer,
# or --tsan to build under ThreadSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/play_missing_file_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "$1" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
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
    test/play_missing_file_test.cpp \
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

"$OUT" "$@"

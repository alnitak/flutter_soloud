#!/bin/bash
# Build and run the standalone native test for seeks in an Ogg (Opus or
# Vorbis) pull stream (see test/pull_ogg_seek_test.cpp): after a seek out of
# the buffer, a later seek must still request the data at its time in the
# file.
#
# It links the plugin's own `bindings.cpp`, so the stream is driven through
# the exports Dart uses, and it needs the Xiph libraries (brew install opus
# libogg libvorbis flac, or the distribution's -dev packages). It plays
# through a real engine on miniaudio's null backend, so no audio hardware is
# used.
#
#   ./test/run_pull_ogg_seek_test.sh [--asan] [file]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. The
# file defaults to both example/assets/audio/sample-OPUS.opus and
# example/assets/audio/sample-vorbis.ogg.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/pull_ogg_seek_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
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

# The OS decoder backends are compiled per platform, like src/CMakeLists.txt.
PLATFORM_SOURCES=()
PLATFORM_LIBS=()
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM_SOURCES=(
        src/native_decoder/os_decoder_apple.mm
        src/audiobuffer/aac_stream_decoder_apple.mm
    )
    PLATFORM_LIBS=(-framework AudioToolbox -framework CoreFoundation
                   -framework Foundation)
else
    PLATFORM_SOURCES=(src/native_decoder/os_decoder_linux.cpp)
fi

c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" \
    -DWITH_MINIAUDIO \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL \
    -DSOLOUD_LIFECYCLE_TEST_HOOKS \
    "${XIPH_FLAGS[@]}" \
    -I src/soloud/include \
    -I src/soloud/src \
    -I src/pffft \
    -I src \
    -I src/native_decoder \
    -o "$OUT" \
    test/pull_ogg_seek_test.cpp \
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
    "${XIPH_LIBS[@]}" \
    "${PLATFORM_LIBS[@]}" \
    -ldl -lm

"$OUT" "$@"

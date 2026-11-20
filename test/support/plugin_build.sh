#!/bin/bash
# Shared by native test scripts that link the whole plugin (bindings.cpp,
# player.cpp, SoLoud with the real miniaudio backend) and drive it through the
# C entry points Dart uses. Source it from the repository root:
#
#   . test/support/plugin_build.sh
#   plugin_build <out> <test.cpp> [extra compiler args...]
#
# Uses $SANITIZER (array, may be empty) and $WORK_DIR (scratch dir). Set
# PLUGIN_BUILD_NO_COMMON=1 to leave out src/soloud_common.cpp, for tests that
# define soloud_platform_log() themselves to read the engine's log.
# Platform: macOS, Linux. Verified on Linux arm64 (test/docker) and macOS.

plugin_build() {
    local out="$1" test_src="$2"
    shift 2
    local platform_sources=() platform_libs=()
    if [ "$(uname)" == "Darwin" ]; then
        platform_sources=(src/native_decoder/os_decoder_apple.mm src/audiobuffer/aac_stream_decoder_apple.mm)
        platform_libs=(-framework AudioToolbox -framework CoreFoundation -framework CoreAudio -framework AudioUnit -framework Foundation -framework AVFoundation)
    else
        platform_sources=(src/native_decoder/os_decoder_linux.cpp)
    fi
    local common=(src/soloud_common.cpp)
    [ "${PLUGIN_BUILD_NO_COMMON:-0}" == "1" ] && common=()

    cc -std=gnu99 -O2 -c ${SANITIZER[@]+"${SANITIZER[@]}"} -I src/pffft \
        -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

    c++ -std=c++17 -O2 -w -pthread ${SANITIZER[@]+"${SANITIZER[@]}"} \
        -DWITH_MINIAUDIO \
        -DNO_XIPH_LIBS \
        -I src/soloud/include \
        -I src/soloud/src \
        -I src/pffft \
        -I src \
        -I src/native_decoder \
        "$@" \
        -o "$out" \
        "$test_src" \
        "$WORK_DIR/pffft.o" \
        ${common[@]+"${common[@]}"} \
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
        "${platform_sources[@]}" \
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
        "${platform_libs[@]}" \
        -ldl -lm
}

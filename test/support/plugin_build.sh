#!/bin/bash
# Shared by native test scripts that link the whole plugin (bindings.cpp,
# player.cpp, SoLoud with the real miniaudio backend) and drive it through the
# C entry points Dart uses. Source it from the repository root:
#
#   . test/support/plugin_build.sh
#   plugin_build <out> <test.cpp> [extra compiler args...]
#
# The plugin's sources come from plugin_sources (test/support/engine_sources.sh).
# Uses $SANITIZER (array, may be empty) and $WORK_DIR (scratch dir). Set
# PLUGIN_BUILD_NO_COMMON=1 to leave out src/soloud_common.cpp, for tests that
# define soloud_platform_log() themselves to read the engine's log.
# Platform: macOS, Linux. Verified on Linux arm64 (test/docker) and macOS.

. "$(dirname "${BASH_SOURCE[0]}")/engine_sources.sh"

plugin_build() {
    local out="$1" test_src="$2"
    shift 2
    plugin_sources || return 1
    local sources=() f
    for f in "${PLUGIN_SOURCES[@]}"; do
        if [ "$f" == "src/soloud_common.cpp" ] && [ "${PLUGIN_BUILD_NO_COMMON:-0}" == "1" ]; then
            continue
        fi
        sources+=("$f")
    done
    local platform_libs=(${PLUGIN_LIBS[@]+"${PLUGIN_LIBS[@]}"})
    if [ "$(uname)" == "Darwin" ]; then
        platform_libs+=(-framework CoreAudio -framework AudioUnit -framework AVFoundation)
    fi

    cc -std=gnu99 -O2 -c ${SANITIZER[@]+"${SANITIZER[@]}"} -I src/pffft \
        -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

    c++ -std=c++17 -O2 -w -pthread ${SANITIZER[@]+"${SANITIZER[@]}"} \
        -DWITH_MINIAUDIO \
        -DNO_XIPH_LIBS \
        "${PLUGIN_INCLUDES[@]}" \
        "$@" \
        -o "$out" \
        "$test_src" \
        "$WORK_DIR/pffft.o" \
        "${sources[@]}" \
        src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
        ${platform_libs[@]+"${platform_libs[@]}"} \
        -ldl -lm
}

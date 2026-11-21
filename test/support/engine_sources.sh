#!/bin/bash
# The sources of the whole native engine (bindings.cpp down to the miniaudio
# backend), for platform tests that drive the FFI entry points the Dart side
# calls (initEngine, play, dispose, ...) on a real audio backend. Source it
# from the repo root, don't run it.
#
#   . test/support/engine_sources.sh
#   engine_sources apple|android|linux   # sets ENGINE_SOURCES, ENGINE_C_SOURCES,
#                                        # ENGINE_INCLUDES, ENGINE_DEFINES
#
# ENGINE_C_SOURCES (pffft.c) must be compiled as C (it is C99), the rest as
# C++17; on Apple the miniaudio backend is the Objective-C++ shim
# src/soloud_miniaudio_objc.mm, as in hook/build.dart. Built without the Xiph
# libraries (NO_XIPH_LIBS) and with the lifecycle test hooks.

engine_sources() {
    local platform="$1"
    ENGINE_C_SOURCES=(src/pffft/pffft.c)
    ENGINE_INCLUDES=(-I src/soloud/include -I src/soloud/src -I src/pffft -I src -I src/native_decoder)
    ENGINE_DEFINES=(-DWITH_MINIAUDIO -DNO_XIPH_LIBS -DSOLOUD_LIFECYCLE_TEST_HOOKS)
    ENGINE_SOURCES=(
        src/soloud_common.cpp
        src/device_lifecycle_test_hooks.cpp
        src/bindings.cpp
        src/player.cpp
        src/analyzer.cpp
        src/synth/*.cpp
        src/waveform/waveform.cpp
        src/waveform/miniaudio_libvorbis.cpp
        src/audiobuffer/*.cpp
        src/native_stream/*.cpp
        src/native_decoder/native_audio_decoder.cpp
        src/filters/*.cpp
        src/mixeroutput/*.cpp
        src/soloud/src/core/*.cpp
        src/soloud/src/filter/*.cpp
        src/soloud/src/audiosource/ay/*.cpp
        src/soloud/src/audiosource/monotone/*.cpp
        src/soloud/src/audiosource/noise/*.cpp
        src/soloud/src/audiosource/sfxr/*.cpp
        src/soloud/src/audiosource/speech/*.cpp
        src/soloud/src/audiosource/tedsid/*.cpp
        src/soloud/src/audiosource/vic/*.cpp
        src/soloud/src/audiosource/vizsn/*.cpp
        src/soloud/src/audiosource/wav/*.cpp
    )
    # The platform-specific decoders are in audiobuffer/*.cpp too: drop the
    # other platforms' ones.
    local filtered=()
    local f
    for f in "${ENGINE_SOURCES[@]}"; do
        case "$f" in
            *_android.cpp) [ "$platform" == "android" ] && filtered+=("$f") ;;
            *_linux.cpp) [ "$platform" == "linux" ] && filtered+=("$f") ;;
            *_windows.cpp|*_web.cpp) ;;
            *_apple.cpp) [ "$platform" == "apple" ] && filtered+=("$f") ;;
            *) filtered+=("$f") ;;
        esac
    done
    ENGINE_SOURCES=("${filtered[@]}")
    case "$platform" in
        apple)
            ENGINE_SOURCES+=(
                src/native_decoder/os_decoder_apple.mm
                src/audiobuffer/aac_stream_decoder_apple.mm
                src/soloud_miniaudio_objc.mm
            )
            ;;
        android)
            ENGINE_SOURCES+=(
                src/native_decoder/os_decoder_android.cpp
                src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp
            )
            ;;
        linux)
            ENGINE_SOURCES+=(
                src/native_decoder/os_decoder_linux.cpp
                src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp
            )
            ;;
    esac
}

# engine_objects <object dir> <cc> <cxx> [extra flags...]
# Compiles ENGINE_C_SOURCES / ENGINE_SOURCES (call engine_sources first) into
# a subdirectory of <object dir> keyed by the state of src/'s headers, in
# parallel, reusing objects newer than their source, and sets ENGINE_OBJECTS.
# <cc>/<cxx> may carry flags ("clang --target=...").
engine_objects() {
    local cc="$2" cxx="$3"
    local key
    key="$(find src -name '*.h' -exec stat -f '%N %m' {} + 2>/dev/null | cksum | cut -d' ' -f1)"
    local dir="$1/$key"
    shift 3
    local extra=("$@")
    mkdir -p "$dir"
    ENGINE_OBJECTS=()
    local jobs=()
    local f obj
    for f in "${ENGINE_C_SOURCES[@]}" "${ENGINE_SOURCES[@]}"; do
        obj="$dir/$(echo "$f" | tr '/' '_').o"
        ENGINE_OBJECTS+=("$obj")
        if [ ! -f "$obj" ] || [ "$f" -nt "$obj" ]; then
            jobs+=("$f")
        fi
    done
    [ "${#jobs[@]}" -eq 0 ] && return 0
    export ENGINE_OBJ_DIR="$dir" ENGINE_CC="$cc" ENGINE_CXX="$cxx"
    export ENGINE_FLAGS="${ENGINE_INCLUDES[*]} ${ENGINE_DEFINES[*]} ${extra[*]}"
    # shellcheck disable=SC2016
    printf '%s\n' "${jobs[@]}" | xargs -n 1 -P "${ENGINE_JOBS:-8}" sh -c '
        f="$1"; obj="$ENGINE_OBJ_DIR/$(echo "$f" | tr "/" "_").o"
        case "$f" in
            *.c) $ENGINE_CC -std=gnu99 -O2 -w $ENGINE_FLAGS -c "$f" -o "$obj" ;;
            *) $ENGINE_CXX -std=c++17 -O2 -w $ENGINE_FLAGS -c "$f" -o "$obj" ;;
        esac || { rm -f "$obj"; echo "failed: $f" >&2; exit 255; }' sh
}

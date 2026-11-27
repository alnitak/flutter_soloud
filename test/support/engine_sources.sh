#!/bin/bash
# The plugin's native sources, for the tests that build the whole plugin and
# drive the FFI entry points the Dart side calls (initEngine, play, dispose,
# ...). Source it from the repo root, don't run it.
#
#   . test/support/engine_sources.sh
#   plugin_sources [apple|android|linux|windows]
#                          # sets PLUGIN_SOURCES, PLUGIN_INCLUDES, PLUGIN_LIBS
#   engine_sources apple|android|linux   # sets ENGINE_SOURCES, ENGINE_C_SOURCES,
#                                        # ENGINE_INCLUDES, ENGINE_DEFINES
#
# plugin_sources reads test/support/plugin_sources.txt, the one list of the
# plugin's sources that every native test build uses: bindings.cpp down to the
# SoLoud engine, with the platform's OS decoder (the host's when no platform
# is given: apple on macOS, linux otherwise). PLUGIN_LIBS has the frameworks
# those sources need on Apple. pffft.c, the audio backend, the defines and
# the compiler flags are left to the caller.
#
# engine_sources is the whole engine for the platform tests (test/macos,
# test/ios, test/android): the plugin's sources without the other platforms'
# decoders, and the miniaudio backend, which on Apple is the Objective-C++
# shim src/soloud_miniaudio_objc.mm, as in hook/build.dart. ENGINE_C_SOURCES
# (pffft.c) must be compiled as C (it is C99), the rest as C++17. Built
# without the Xiph libraries (NO_XIPH_LIBS) and with the lifecycle test hooks.

PLUGIN_SOURCES_LIST="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/plugin_sources.txt"

plugin_sources() {
    local platform="${1:-}"
    if [ -z "$platform" ]; then
        if [ "$(uname)" == "Darwin" ]; then
            platform=apple
        else
            platform=linux
        fi
    fi
    PLUGIN_INCLUDES=(-I src/soloud/include -I src/soloud/src -I src/pffft -I src -I src/native_decoder)
    PLUGIN_LIBS=()
    if [ "$platform" == "apple" ]; then
        PLUGIN_LIBS=(-framework AudioToolbox -framework CoreFoundation -framework Foundation)
    fi
    PLUGIN_SOURCES=()
    local line entry f
    while IFS= read -r line || [ -n "$line" ]; do
        entry="${line%%#*}"
        entry="${entry%$'\r'}"
        case "$entry" in
            apple:*|android:*|linux:*|windows:*)
                [ "${entry%%:*}" == "$platform" ] || continue
                entry="${entry#*:}"
                ;;
        esac
        # Unquoted on purpose: it trims the line and expands the glob.
        for f in $entry; do
            if [ ! -f "$f" ]; then
                echo "plugin_sources: $f (from $PLUGIN_SOURCES_LIST) not found" >&2
                return 1
            fi
            PLUGIN_SOURCES+=("$f")
        done
    done < "$PLUGIN_SOURCES_LIST"
}

engine_sources() {
    local platform="$1"
    plugin_sources "$platform" || return 1
    ENGINE_C_SOURCES=(src/pffft/pffft.c)
    ENGINE_INCLUDES=("${PLUGIN_INCLUDES[@]}")
    ENGINE_DEFINES=(-DWITH_MINIAUDIO -DNO_XIPH_LIBS -DSOLOUD_LIFECYCLE_TEST_HOOKS)
    # The platform-specific decoders are in audiobuffer/*.cpp too: drop the
    # other platforms' ones.
    ENGINE_SOURCES=()
    local f
    for f in "${PLUGIN_SOURCES[@]}"; do
        case "$f" in
            *_android.cpp) [ "$platform" == "android" ] && ENGINE_SOURCES+=("$f") ;;
            *_linux.cpp) [ "$platform" == "linux" ] && ENGINE_SOURCES+=("$f") ;;
            *_windows.cpp|*_web.cpp) ;;
            *_apple.cpp) [ "$platform" == "apple" ] && ENGINE_SOURCES+=("$f") ;;
            *) ENGINE_SOURCES+=("$f") ;;
        esac
    done
    if [ "$platform" == "apple" ]; then
        ENGINE_SOURCES+=(src/soloud_miniaudio_objc.mm)
    else
        ENGINE_SOURCES+=(src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp)
    fi
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

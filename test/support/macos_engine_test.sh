#!/bin/bash
# Builds and runs one macOS test that links the whole engine
# (test/support/engine_sources.sh, with the real CoreAudio backend through
# src/soloud_miniaudio_objc.mm). Used by the test/macos/*_test.sh scripts that
# drive the FFI entry points on real devices. Source it, then:
#
#   macos_engine_test <name> "$@"
#
# builds test/macos/<name>.mm and runs it with the remaining arguments.
# Engine objects are cached under $TMPDIR/soloud_macos_engine_objs.

. test/support/engine_sources.sh

macos_engine_test() {
    local name="$1"
    shift
    engine_sources apple
    engine_objects "${TMPDIR:-/tmp}/soloud_macos_engine_objs/$(uname -m)" "cc" "c++" -DNDEBUG
    local out="${TMPDIR:-/tmp}/${name}"
    c++ -std=c++17 -O1 -g -w -fobjc-arc \
        "${ENGINE_INCLUDES[@]}" "${ENGINE_DEFINES[@]}" -DNDEBUG \
        -o "$out" "test/macos/$name.mm" "${ENGINE_OBJECTS[@]}" \
        -framework Foundation -framework AVFoundation -framework AudioToolbox -framework CoreAudio \
        -framework Accelerate -framework CoreFoundation
    "$out" "$@"
}

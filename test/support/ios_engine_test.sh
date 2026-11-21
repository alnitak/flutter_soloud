#!/bin/bash
# Builds and runs one iOS-simulator test that links the whole engine
# (test/support/engine_sources.sh, with the Objective-C++ miniaudio shim and
# the real CoreAudio backend, AVAudioSession included). Used by the
# test/ios/*_test.sh scripts that drive the FFI entry points. Source it, then:
#
#   ios_engine_test <name> "$@"
#
# builds test/ios/<name>.mm and runs it with `xcrun simctl spawn` (see
# test/support/ios_simulator.sh), passing --strict through. Engine objects are
# cached under $TMPDIR/soloud_iossim_engine_objs. Prints SKIPPED without the
# SDK, a runtime or a simulator.

. test/support/ios_simulator.sh
. test/support/engine_sources.sh

ios_engine_test() {
    local name="$1"
    shift
    local args=()
    for arg in "$@"; do
        [ "$arg" == "--strict" ] && args+=(--strict)
    done
    ios_sim_require || exit 0
    engine_sources apple
    local sdk
    sdk="$(xcrun -sdk iphonesimulator --show-sdk-path)"
    local cc="xcrun -sdk iphonesimulator clang -arch $IOS_SIM_ARCH -mios-simulator-version-min=$IOS_SIM_MIN"
    local cxx="xcrun -sdk iphonesimulator clang++ -arch $IOS_SIM_ARCH -mios-simulator-version-min=$IOS_SIM_MIN"
    engine_objects "${TMPDIR:-/tmp}/soloud_iossim_engine_objs/$IOS_SIM_ARCH" "$cc" "$cxx" -DNDEBUG
    local out="${TMPDIR:-/tmp}/${name}_iossim"
    ios_sim_cxx -std=c++17 -O1 -g -w -fobjc-arc \
        "${ENGINE_INCLUDES[@]}" "${ENGINE_DEFINES[@]}" -DNDEBUG \
        -o "$out" "test/ios/$name.mm" "${ENGINE_OBJECTS[@]}" \
        -framework Foundation -framework AVFoundation -framework AVFAudio -framework AudioToolbox \
        -framework CoreAudio -framework Accelerate -framework CoreFoundation
    trap 'ios_sim_release' EXIT
    ios_sim_acquire || exit 0
    echo "Simulator: $IOS_SIM_UDID ($(xcrun simctl list devices | grep "$IOS_SIM_UDID" | sed 's/ (.*//' | xargs))"
    ios_sim_run "$out" "${args[@]}"
}

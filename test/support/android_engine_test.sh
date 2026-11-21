#!/bin/bash
# Builds and runs one Android native test that links the whole engine
# (test/support/engine_sources.sh) on a device or emulator. Used by the
# test/android/*_test.sh scripts that drive the FFI entry points on the real
# AAudio/OpenSL backends. Source it, then:
#
#   android_engine_test <name> [min API] -- "$@"
#
# builds test/android/<name>.cpp for the device's ABI (API: the device's,
# capped at 31, and at least [min API]), and runs it with the script's
# remaining arguments (--strict, ...). Handles --build-only and SKIPPED like
# test/support/android_ndk.sh. Engine objects are cached under
# $TMPDIR/soloud_android_engine_objs.

. test/support/android_ndk.sh
. test/support/engine_sources.sh

android_engine_test() {
    local name="$1" min_api="$2"
    shift 2
    [ "${1:-}" == "--" ] && shift
    android_parse_args "$@"
    android_require || exit 0
    if [ "$BUILD_ONLY" == "1" ]; then
        API="${API:-31}"
    fi
    android_target
    if [ "$API" -lt "$min_api" ]; then
        API="$min_api"
        android_target
    fi
    engine_sources android
    local objs="${TMPDIR:-/tmp}/soloud_android_engine_objs/$ABI-$API"
    engine_objects "$objs" "${CC[*]}" "${CXX[*]}" -O3 -ffast-math -fvisibility=hidden -DNDEBUG
    local out="${TMPDIR:-/tmp}/$name"
    "${CXX[@]}" -std=c++17 -O2 -w -static-libstdc++ \
        "${ENGINE_INCLUDES[@]}" "${ENGINE_DEFINES[@]}" -DNDEBUG \
        -o "$out" test/android/"$name".cpp "${ENGINE_OBJECTS[@]}" \
        -lmediandk -llog -ldl -lm -landroid
    if [ "$BUILD_ONLY" == "1" ]; then
        echo "Built $ABI (API $API): $out"
        exit 0
    fi
    echo "Device: $("${ADB[@]}" shell getprop ro.product.model | tr -d '\r'), API $(android_device_api), $ABI"
    android_run "$out" "${ANDROID_ARGS[@]}"
}

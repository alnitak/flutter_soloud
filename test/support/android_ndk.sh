#!/bin/bash
# Helpers for test/android/*_test.sh scripts: find the NDK and adb (also when
# adb is not on PATH), pick the target ABI and API, and run a binary on the
# device. Source it, don't run it.
#
#   . test/support/android_ndk.sh
#   android_parse_args "$@"            # --build-only, [adb-serial]; sets BUILD_ONLY, ADB
#   android_require || exit 0          # prints SKIPPED and returns 1 if no NDK / device
#   android_target                     # sets ABI, API, TARGET, CC, CXX, HOST_TAG
#   "$CXX" ... -o "$OUT" ...
#   android_run "$OUT" args...         # pushes to /data/local/tmp/<name> and runs it
#
# Env: ANDROID_HOME (default ~/Library/Android/sdk), ANDROID_NDK_HOME (default
# the newest NDK under $ANDROID_HOME/ndk), ABI (default the device's primary
# ABI, or arm64-v8a with --build-only), API (default min(device API, 31), or
# 21 with --build-only), ANDROID_SERIAL / the first script argument pick the
# device.

ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
BUILD_ONLY=0
ADB=()

android_parse_args() {
    ANDROID_ARGS=()
    local serial="${ANDROID_SERIAL:-}"
    for arg in "$@"; do
        case "$arg" in
            --build-only) BUILD_ONLY=1 ;;
            --*) ANDROID_ARGS+=("$arg") ;;
            *) if [ -z "$serial" ]; then serial="$arg"; else ANDROID_ARGS+=("$arg"); fi ;;
        esac
    done
    local adb_bin
    adb_bin="$(command -v adb || true)"
    if [ -z "$adb_bin" ] && [ -x "$ANDROID_HOME/platform-tools/adb" ]; then
        adb_bin="$ANDROID_HOME/platform-tools/adb"
    fi
    ADB=("${adb_bin:-adb}")
    if [ -n "$serial" ]; then
        ADB+=(-s "$serial")
    fi
}

android_require() {
    if [ -z "${ANDROID_NDK_HOME:-}" ] && ! ls -d "$ANDROID_HOME"/ndk/* > /dev/null 2>&1; then
        echo "SKIPPED: no Android NDK (set ANDROID_NDK_HOME or install one under $ANDROID_HOME/ndk)"
        return 1
    fi
    if [ "$BUILD_ONLY" == "1" ]; then
        return 0
    fi
    if ! command -v "${ADB[0]}" > /dev/null 2>&1; then
        echo "SKIPPED: adb not found (use --build-only to just build)"
        return 1
    fi
    if [ "$("${ADB[@]}" get-state 2>/dev/null)" != "device" ]; then
        echo "SKIPPED: no Android device or emulator connected (use --build-only to just build)"
        return 1
    fi
    return 0
}

android_target() {
    NDK="${ANDROID_NDK_HOME:-$(ls -d "$ANDROID_HOME"/ndk/* | sort -V | tail -1)}"
    HOST_TAG="$(ls "$NDK/toolchains/llvm/prebuilt" | head -1)"
    if [ "$BUILD_ONLY" == "1" ]; then
        ABI="${ABI:-arm64-v8a}"
        API="${API:-21}"
    else
        ABI="${ABI:-$("${ADB[@]}" shell getprop ro.product.cpu.abi | tr -d '\r')}"
        if [ -z "${API:-}" ]; then
            local device_api
            device_api="$("${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r')"
            API=$(( device_api < 31 ? device_api : 31 ))
        fi
    fi
    case "$ABI" in
        arm64-v8a) TARGET=aarch64-linux-android ;;
        armeabi-v7a) TARGET=armv7a-linux-androideabi ;;
        x86_64) TARGET=x86_64-linux-android ;;
        x86) TARGET=i686-linux-android ;;
        *) echo "Unsupported ABI: $ABI"; return 1 ;;
    esac
    local bin="$NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
    CC=("$bin/clang" "--target=$TARGET$API")
    CXX=("$bin/clang++" "--target=$TARGET$API")
}

android_device_api() {
    "${ADB[@]}" shell getprop ro.build.version.sdk | tr -d '\r'
}

# android_run <binary> [args...]: pushes the binary to /data/local/tmp, runs it
# and deletes it; returns the binary's exit code.
android_run() {
    local bin="$1"
    shift
    local name
    name="$(basename "$bin")"
    "${ADB[@]}" push "$bin" "/data/local/tmp/$name" > /dev/null
    "${ADB[@]}" shell "chmod 755 /data/local/tmp/$name && /data/local/tmp/$name $*; echo \"__EXIT=\$?\"; rm -f /data/local/tmp/$name" \
        | tee "${TMPDIR:-/tmp}/$name.log"
    local code
    code="$(grep -o '__EXIT=[0-9]*' "${TMPDIR:-/tmp}/$name.log" | tail -1 | cut -d= -f2)"
    return "${code:-1}"
}

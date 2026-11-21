#!/bin/bash
# Android ABI matrix: builds the Android native tests for armeabi-v7a,
# arm64-v8a and x86_64 (each test/android/*_test.sh that takes --build-only,
# with ABI / ANDROID_ABI set), so code that only breaks on 32-bit ARM or x86
# (size_t/off_t widths, alignment, NEON vs SSE paths) fails here. Then the
# bounds tests that matter most on 32 bit (test/common/metadata_bounds_test,
# N3, and test/common/m4a_gapless_test, whichever exist) are built for
# every ABI with the NDK and run on the attached device for each ABI it
# supports (ro.product.cpu.abilist): 32-bit ARM needs a device or image with
# armeabi-v7a support.
#
#   test/android/android_abi_build_test.sh [--build-only] [--strict] [adb-serial]
#
# Platform: any host with the NDK (builds); a device or emulator for the
# runs. Without adb/device (or with --build-only) only the builds are done;
# an ABI the device cannot run is reported as SKIPPED for its runs. Exits
# non-zero if any build or run fails; runs failing because of a known src/
# bug (listed in known_bug below) are reported as KNOWN BUG and fail only
# with --strict. Env: ABIS (default
# "armeabi-v7a arm64-v8a x86_64"), see also test/support/android_ndk.sh.
# Verification: builds verified for all three ABIs with NDK 29 on macOS arm64;
# runs on a Pixel 8 API 34 arm64 emulator (64-bit only: 32-bit runs SKIPPED)
# and on a Galaxy S22 Ultra, API 36 (arm64-v8a and armeabi-v7a runs).

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
. test/support/android_ndk.sh

android_parse_args "$@"
if [ -z "${ANDROID_NDK_HOME:-}" ] && ! ls -d "$ANDROID_HOME"/ndk/* > /dev/null 2>&1; then
    echo "SKIPPED: no Android NDK"
    exit 0
fi
HAVE_DEVICE=0
if [ "$BUILD_ONLY" == "0" ] && command -v "${ADB[0]}" > /dev/null 2>&1 &&
    [ "$("${ADB[@]}" get-state 2>/dev/null)" == "device" ]; then
    HAVE_DEVICE=1
    DEVICE_ABIS=" $("${ADB[@]}" shell getprop ro.product.cpu.abilist | tr -d '\r' | tr ',' ' ') "
    echo "Device ABIs:$DEVICE_ABIS"
fi

ABIS="${ABIS:-armeabi-v7a arm64-v8a x86_64}"
LOG_DIR="$(mktemp -d)"
trap 'rm -rf "$LOG_DIR"' EXIT
FAILED=()
PASSED=()
SKIPPED=()
KNOWN=()
STRICT=0
for arg in "$@"; do [ "$arg" == "--strict" ] && STRICT=1; done
# Runs that fail because of a known src/ bug: "<test> <abi>" -> bug ID. They
# are reported as KNOWN BUG and fail the script only with --strict.
known_bug() {
    # None at the moment. Add cases as: "<test> <abi>") echo <ID> ;;
    case "$1 $2" in
        *) ;;
    esac
}

# 1. Every Android test script that can build without a device.
for abi in $ABIS; do
    for script in test/android/*_test.sh; do
        name="$(basename "$script" .sh)"
        [ "$name" == "android_abi_build_test" ] && continue
        grep -q -- "--build-only" "$script" || continue
        log="$LOG_DIR/$name-$abi.log"
        if ABI="$abi" ANDROID_ABI="$abi" ANDROID_API=30 "$script" --build-only > "$log" 2>&1; then
            PASSED+=("build $name [$abi]")
        else
            FAILED+=("build $name [$abi]")
            tail -20 "$log"
        fi
    done
done

# 2. The bounds tests, built with the NDK for every ABI and run where possible.
BOUNDS=()
[ -f test/common/metadata_bounds_test.cpp ] && BOUNDS+=(metadata_bounds_test)
[ -f test/common/m4a_gapless_test.cpp ] && BOUNDS+=(m4a_gapless_test)
[ "${#BOUNDS[@]}" -eq 0 ] && SKIPPED+=("bounds tests: neither metadata_bounds_test nor m4a_gapless_test exists")

# The sources each bounds test links, taken from its own script: every
# src/ or test/ .c/.cpp argument on the compile command (so the list cannot
# drift from the host build).
bounds_sources() {
    grep -o -E '(src|test)/[A-Za-z0-9_/.*$-]+\.(cpp|c)' "test/common/$1.sh" | sed "s/\$NAME/$1/g" | sort -u
}

for abi in $ABIS; do
    ABI="$abi"
    API=21
    android_target
    for name in "${BOUNDS[@]}"; do
        out="${TMPDIR:-/tmp}/${name}_$abi"
        # shellcheck disable=SC2207
        srcs=($(bounds_sources "$name"))
        log="$LOG_DIR/$name-$abi-build.log"
        if "${CXX[@]}" -std=c++17 -O2 -w -static-libstdc++ -I src -I src/audiobuffer \
            -o "$out" "${srcs[@]}" -ldl -lm > "$log" 2>&1; then
            PASSED+=("build $name [$abi]")
        else
            FAILED+=("build $name [$abi]")
            tail -20 "$log"
            continue
        fi
        if [ "$HAVE_DEVICE" == "0" ]; then
            SKIPPED+=("run $name [$abi]: no device")
            continue
        fi
        case "$DEVICE_ABIS" in
            *" $abi "*) ;;
            *) SKIPPED+=("run $name [$abi]: the device does not run $abi"); continue ;;
        esac
        echo "== $name [$abi] on the device"
        if android_run "$out" > "$LOG_DIR/$name-$abi-run.log" 2>&1; then
            PASSED+=("run $name [$abi]")
            tail -3 "$LOG_DIR/$name-$abi-run.log"
        else
            bug="$(known_bug "$name" "$abi")"
            tail -30 "$LOG_DIR/$name-$abi-run.log"
            if [ -n "$bug" ] && [ "$STRICT" == "0" ]; then
                KNOWN+=("run $name [$abi]: KNOWN BUG $bug")
            else
                FAILED+=("run $name [$abi]${bug:+ (bug $bug)}")
            fi
        fi
    done
done

echo
for p in "${PASSED[@]}"; do echo "PASS    $p"; done
for s in "${SKIPPED[@]}"; do echo "SKIPPED: $s"; done
for k in "${KNOWN[@]}"; do echo "KNOWN BUG $k"; done
for f in "${FAILED[@]}"; do echo "FAIL    $f"; done
if [ "${#FAILED[@]}" -gt 0 ]; then
    echo "${#FAILED[@]} build/run step(s) failed."
    exit 1
fi
echo "All ABI builds and runs passed (${#PASSED[@]} steps)."

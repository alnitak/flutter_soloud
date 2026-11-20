#!/bin/bash
# Build and run the standalone tests of src/vst3/vst3_param_changes.h (see
# test/common/vst3_param_changes_test.cpp): the lock-free SPSC queue of
# parameter changes / MIDI notes (FIFO, full, clear(), producer + consumer
# threads) and the preallocated IParameterChanges, IParamValueQueue and
# IEventList handed to the plug-in (sampleOffset order, several points per
# parameter, limits).
#
# Needs no audio device and no assets. Needs the VST3 SDK headers and the
# hosting library (for the interface iids) that the build hook downloads when
# VST3 is enabled: .dart_tool/flutter_soloud/vst3/prebuild/{include,
# macos/libvst3_hosting.a | linux/<x64|arm64>/libvst3_hosting.a}, or
# VST3_PREBUILD_DIR. Prints SKIPPED without them.
#
#   ./test/common/vst3_param_changes_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=vst3_param_changes_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

PREBUILD="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
case "$(uname -s)/$(uname -m)" in
    Darwin/*) SDK_LIB="$PREBUILD/macos/libvst3_hosting.a" ;;
    Linux/x86_64) SDK_LIB="$PREBUILD/linux/x64/libvst3_hosting.a" ;;
    Linux/aarch64|Linux/arm64) SDK_LIB="$PREBUILD/linux/arm64/libvst3_hosting.a" ;;
    *) SDK_LIB="" ;;
esac
if [ ! -f "$PREBUILD/include/pluginterfaces/vst/ivstparameterchanges.h" ] ||
   [ -z "$SDK_LIB" ] || [ ! -f "$SDK_LIB" ]; then
    echo "SKIPPED: VST3 SDK not found in $PREBUILD. Build the example app" \
        "once with VST3 enabled, or set VST3_PREBUILD_DIR."
    exit 0
fi

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -DSOLOUD_ENABLE_VST3 \
    -I src \
    -isystem "$PREBUILD/include" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    "$SDK_LIB"

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

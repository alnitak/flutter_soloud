#!/bin/bash
# Build and run the mixer-output capture ring tests (test/mixer_output_test.cpp).
# They drive MixerOutput directly, so no engine or audio device is needed.
#
#   ./test/run_mixer_output_test.sh [--asan|--tsan] [--web-model] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --web-model builds the capture's web path on the host:
# __EMSCRIPTEN__ for mixer_output.cpp and the test (no worker threads,
# notifications from the audio callback, the packet pool). A scenario name
# runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g)
    SUFFIX="_tsan"
    shift
fi

WEB_MODEL=0
if [ "${1:-}" == "--web-model" ]; then
    WEB_MODEL=1
    SUFFIX="${SUFFIX}_web"
    shift
fi

OUT="${TMPDIR:-/tmp}/mixer_output_test$SUFFIX"
FLAGS=(-std=c++17 -O1 -w -pthread "${SANITIZER[@]}" -DNO_XIPH_LIBS -I src
    -I src/soloud/include)

if [ "$WEB_MODEL" == 0 ]; then
    c++ "${FLAGS[@]}" -o "$OUT" test/mixer_output_test.cpp \
        src/mixeroutput/*.cpp
else
    # Only mixer_output.{h,cpp} have a web path. The encoders build as they
    # do natively: the SIMD header they share has no host path for
    # __EMSCRIPTEN__ without wasm SIMD, and neither TU built with it includes
    # that header.
    OBJECTS="$OUT.objects"
    rm -rf "$OBJECTS"
    mkdir -p "$OBJECTS"
    LINK=()
    for source in test/mixer_output_test.cpp src/mixeroutput/*.cpp; do
        MODEL=()
        case "$source" in
        test/mixer_output_test.cpp | src/mixeroutput/mixer_output.cpp)
            MODEL=(-D__EMSCRIPTEN__)
            ;;
        esac
        object="$OBJECTS/$(basename "$source" .cpp).o"
        c++ "${FLAGS[@]}" "${MODEL[@]}" -c -o "$object" "$source"
        LINK+=("$object")
    done
    c++ "${FLAGS[@]}" -o "$OUT" "${LINK[@]}"
fi

"$OUT" "$@"

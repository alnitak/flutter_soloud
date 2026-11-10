#!/bin/bash
# Build and run the metadata parser robustness tests
# (test/metadata_parsers_test.cpp). They drive the stream decoders directly,
# so no engine or audio device is needed. The host build needs the Xiph
# libraries (brew install opus libogg libvorbis flac) and links the Apple AAC
# decoder, so it runs on macOS.
#
#   ./test/run_metadata_parsers_test.sh [--asan|--wasm32] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --wasm32 builds with em++ and runs the result under node, so the parsers
# run with a 32-bit size_t and a WebAssembly stack, as in the web build (and,
# for size_t, on armeabi-v7a). It builds without the Xiph libraries
# (NO_XIPH_LIBS), so the checks that need them are left out. If em++'s own
# node is broken, point EM_NODE_JS at a working one.
#
# A scenario name (see the test) runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

DECODERS=(
    src/audiobuffer/stream_decoder.cpp
    src/audiobuffer/m4a_metadata.cpp
    src/audiobuffer/mp3_stream_decoder.cpp
    src/audiobuffer/wav_stream_decoder.cpp
    src/audiobuffer/aac_stream_decoder.cpp
    src/audiobuffer/opus_stream_decoder.cpp
    src/audiobuffer/vorbis_stream_decoder.cpp
    src/audiobuffer/flac_stream_decoder.cpp
    src/audiobuffer/ogg_flac_stream_decoder.cpp
    src/soloud/src/audiosource/wav/dr_impl.cpp
)
INCLUDES=(-I src -I src/soloud/include -I src/soloud/src -I src/native_decoder)

if [ "${1:-}" == "--wasm32" ]; then
    shift
    OUT="${TMPDIR:-/tmp}/metadata_parsers_test_wasm.js"
    # Memory and stack settings follow web/compile_wasm.sh.
    em++ -std=c++17 -O2 -w -pthread -msimd128 -DNO_XIPH_LIBS "${INCLUDES[@]}" \
        -s STACK_SIZE=4194304 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s INITIAL_MEMORY=67108864 \
        -s MAXIMUM_MEMORY=2147483648 \
        -s PTHREAD_POOL_SIZE=2 \
        -s EXIT_RUNTIME=1 \
        -o "$OUT" \
        test/metadata_parsers_test.cpp "${DECODERS[@]}" \
        src/audiobuffer/aac_stream_decoder_web.cpp
    "${EM_NODE_JS:-node}" "$OUT" "$@"
    exit $?
fi

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    SUFFIX="_asan"
    shift
fi

XIPH_FLAGS=()
XIPH_LIBS=()
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/ogg/ogg.h" ]; then
        XIPH_FLAGS=(-I "$prefix/include")
        XIPH_LIBS=(-L "$prefix/lib" -lvorbis -logg -lopus -lFLAC)
        break
    fi
done
if [ ${#XIPH_LIBS[@]} -eq 0 ]; then
    echo "Xiph libraries not found (brew install opus libogg libvorbis flac)" >&2
    exit 2
fi

if [ "$(uname)" != "Darwin" ]; then
    echo "The host build links the Apple AAC decoder: run it on macOS" >&2
    exit 2
fi
PLATFORM=(src/audiobuffer/aac_stream_decoder_apple.mm
          -framework AudioToolbox -framework CoreFoundation)

OUT="${TMPDIR:-/tmp}/metadata_parsers_test$SUFFIX"
c++ -std=c++17 -O2 -w -pthread "${SANITIZER[@]}" "${INCLUDES[@]}" "${XIPH_FLAGS[@]}" \
    -o "$OUT" test/metadata_parsers_test.cpp "${DECODERS[@]}" "${PLATFORM[@]}" \
    "${XIPH_LIBS[@]}"
"$OUT" "$@"

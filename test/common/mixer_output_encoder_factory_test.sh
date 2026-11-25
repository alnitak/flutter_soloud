#!/bin/bash
# Build and run the mixer-output encoder factory tests
# (test/common/mixer_output_encoder_factory_test.cpp): create() for every
# MixerOutputFormat, encoder lifecycle, initialize() with channel counts and
# rates at and beyond each format's limits (0 and negative included). Built
# twice: with the Xiph libraries, and with -DNO_XIPH_LIBS, where FLAC,
# Vorbis and Opus must refuse to initialize.
#
# Device: none. Assets: none. Needs libogg, libvorbis, libopus and libflac
# for the Xiph build (Homebrew on macOS, -dev packages on Linux); without
# them only the NO_XIPH_LIBS build runs.
#
#   ./test/common/mixer_output_encoder_factory_test.sh [--asan] [--strict] [case]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict makes the checks marked KNOWN BUG fail the run. A case name runs
# only that case. XIPH_PREFIX overrides where the Xiph libraries are.
# Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ -n "${XIPH_PREFIX:-}" ]; then
    XIPH="$XIPH_PREFIX"
elif [ "$(uname)" == "Darwin" ] && command -v brew > /dev/null 2>&1; then
    XIPH="$(brew --prefix)"
elif [ "$(uname)" == "Darwin" ]; then
    XIPH=/usr/local
else
    XIPH=/usr
fi

HAVE_XIPH=1
for header in ogg/ogg.h vorbis/vorbisenc.h opus/opus.h FLAC/stream_encoder.h; do
    if [ ! -f "$XIPH/include/$header" ]; then
        echo "note: $header not found under $XIPH/include: NO_XIPH_LIBS build only"
        HAVE_XIPH=0
    fi
done

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    # libvorbisenc's own leak with > 2 channels, see test/support/lsan.supp.
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
    shift
fi

SOURCES=(
    src/mixeroutput/mixer_output_encoder.cpp
    src/mixeroutput/wav_output_encoder.cpp
    src/mixeroutput/flac_output_encoder.cpp
    src/mixeroutput/opus_output_encoder.cpp
    src/mixeroutput/vorbis_output_encoder.cpp
)

OUT_NOXIPH="${TMPDIR:-/tmp}/mixer_output_encoder_factory_test_noxiph$SUFFIX"
c++ -std=c++17 -O1 -w "${SANITIZER[@]}" -DNO_XIPH_LIBS \
    -I src \
    -I src/soloud/include \
    -o "$OUT_NOXIPH" \
    test/common/mixer_output_encoder_factory_test.cpp \
    "${SOURCES[@]}"

if [ "$HAVE_XIPH" == "1" ]; then
    OUT="${TMPDIR:-/tmp}/mixer_output_encoder_factory_test$SUFFIX"
    c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
        -I src \
        -I src/soloud/include \
        -I "$XIPH/include" \
        -o "$OUT" \
        test/common/mixer_output_encoder_factory_test.cpp \
        "${SOURCES[@]}" \
        -L "$XIPH/lib" -Wl,-rpath,"$XIPH/lib" \
        -lFLAC -lvorbisenc -lvorbis -lopus -logg
    "$OUT" "$@"
fi

"$OUT_NOXIPH" "$@"

#!/bin/bash
# Build and run the mixer-output Ogg Vorbis encoder tests
# (test/common/vorbis_output_encoder_test.cpp): encode -> libvorbisfile
# decode roundtrip, sample-exact duration, per-channel sine fidelity (RMS,
# frequency, correlation), silence, empty stream, lifecycle. No audio device
# or assets needed. Needs libogg + libvorbis/vorbisenc/vorbisfile (Homebrew
# `libvorbis` on macOS, libvorbis-dev on Linux); prints SKIPPED if missing.
#
#   ./test/common/vorbis_output_encoder_test.sh [--asan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
# scenario name runs only that scenario. XIPH_PREFIX overrides where the Xiph
# libraries are installed (default: `brew --prefix` on macOS, /usr on Linux).

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

for header in ogg/ogg.h vorbis/codec.h vorbis/vorbisenc.h vorbis/vorbisfile.h; do
    if [ ! -f "$XIPH/include/$header" ]; then
        echo "SKIPPED: $header not found under $XIPH/include (set XIPH_PREFIX)"
        exit 0
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

OUT="${TMPDIR:-/tmp}/vorbis_output_encoder_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
    -I src \
    -I src/soloud/include \
    -I "$XIPH/include" \
    -o "$OUT" \
    test/common/vorbis_output_encoder_test.cpp \
    src/mixeroutput/vorbis_output_encoder.cpp \
    -L "$XIPH/lib" -Wl,-rpath,"$XIPH/lib" -lvorbisfile -lvorbisenc -lvorbis -logg

"$OUT" "$@"

#!/bin/bash
# Build and run the mixer-output encoder fuzz tests
# (test/common/output_encoders_fuzz_test.cpp): the WAV, FLAC, Vorbis and
# Opus encoders fed random blocks with NaN, +-Inf, huge values, subnormals,
# 0-sample blocks, at the channel/rate limits and finalized with no data;
# every stream is decoded again with the reference library and checked for
# validity, exact length and finite, bounded (WAV/FLAC: exact) samples.
#
# Device: none. Assets: none. Needs libogg, libvorbis, libopus and libflac
# (Homebrew on macOS, -dev packages on Linux); prints SKIPPED if missing.
#
#   ./test/common/output_encoders_fuzz_test.sh [--asan] [--strict] [case]
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

for header in ogg/ogg.h vorbis/vorbisenc.h vorbis/vorbisfile.h opus/opus.h \
              FLAC/stream_encoder.h FLAC/stream_decoder.h; do
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

OUT="${TMPDIR:-/tmp}/output_encoders_fuzz_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
    -I src \
    -I src/soloud/include \
    -I "$XIPH/include" \
    -o "$OUT" \
    test/common/output_encoders_fuzz_test.cpp \
    src/mixeroutput/wav_output_encoder.cpp \
    src/mixeroutput/flac_output_encoder.cpp \
    src/mixeroutput/opus_output_encoder.cpp \
    src/mixeroutput/vorbis_output_encoder.cpp \
    -L "$XIPH/lib" -Wl,-rpath,"$XIPH/lib" \
    -lFLAC -lvorbisfile -lvorbisenc -lvorbis -lopus -logg

"$OUT" "$@"

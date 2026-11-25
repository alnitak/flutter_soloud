#!/bin/bash
# Build and run the mixer-output WAV encoder tests
# (test/common/wav_output_encoder_test.cpp): RIFF/WAVE header fields for
# mono/stereo/multichannel at several rates, payload/data sizes across chunks,
# int16 sample values, lifecycle. The encoder only writes 16-bit PCM WAV.
# No audio device, assets or Xiph libraries needed (built with -DNO_XIPH_LIBS
# so the encoder factory links without them).
#
#   ./test/common/wav_output_encoder_test.sh [--asan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
# scenario name runs only that scenario.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
fi

OUT="${TMPDIR:-/tmp}/wav_output_encoder_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" -DNO_XIPH_LIBS \
    -I src \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/wav_output_encoder_test.cpp \
    src/mixeroutput/wav_output_encoder.cpp \
    src/mixeroutput/mixer_output_encoder.cpp \
    src/mixeroutput/flac_output_encoder.cpp \
    src/mixeroutput/opus_output_encoder.cpp \
    src/mixeroutput/vorbis_output_encoder.cpp

"$OUT" "$@"

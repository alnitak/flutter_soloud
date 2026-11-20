#!/bin/bash
# Build and run the unit tests of loading whole sounds into memory with
# SoLoud::Wav (test/common/soloud_wav_load_test.cpp; soloud_wav.cpp and
# dr_impl.cpp): WAV 8/16/24/32-bit and float, FLAC encoded at test time,
# 8 kHz to 192 kHz, 1 to 8 channels; truncated WAV, FLAC, MP3 and Ogg; headers
# without data; voices, seek and reloads. The OS decoder fallback is stubbed.
#
# Needs no audio device. Uses the MP3 and Ogg files in example/assets/audio,
# and the Xiph libraries and headers (brew install opus libogg libvorbis flac;
# apt install libogg-dev libvorbis-dev libopus-dev libflac-dev); without them
# the test is skipped.
#
#   ./test/common/soloud_wav_load_test.sh [--asan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=soloud_wav_load_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

XIPH_PREFIX=""
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/FLAC/stream_encoder.h" ] && [ -f "$prefix/include/ogg/ogg.h" ] &&
       [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/vorbis/vorbisfile.h" ]; then
        XIPH_PREFIX="$prefix"
        break
    fi
done
if [ -z "$XIPH_PREFIX" ]; then
    echo "SKIPPED: Xiph libraries not found (brew install opus libogg libvorbis flac)"
    exit 0
fi

# dr_mp3's implementation is compiled in src/audiobuffer/mp3_stream_decoder.cpp,
# like in the plugin.
c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I src/soloud/include \
    -I src/soloud/src/audiosource/wav \
    -I src/native_decoder \
    -I "$XIPH_PREFIX/include" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wav.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/soloud/src/audiosource/wav/mb_ogg.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    -L "$XIPH_PREFIX/lib" -lFLAC -lvorbisfile -lvorbis -logg -lopus -lm

"$OUT" example/assets/audio "$@"

#!/bin/bash
# Build and run the fixed-seed fuzz tests of the metadata parsers
# (test/common/metadata_parsers_fuzz_test.cpp): ID3 text and tags (AAC parser
# and MP3 decoder), M4A boxes and gapless info, ICY metadata, OpusTags and
# Vorbis comments, FLAC / Ogg FLAC comments and pictures. Random bytes and
# corrupted valid files, each input in a child process (POSIX).
#
# Needs no audio device. Reads example/assets/audio for seeds (groups whose
# seeds are missing are skipped). Needs the Xiph libraries and headers (brew
# install opus libogg libvorbis flac; apt install libogg-dev libvorbis-dev
# libopus-dev libflac-dev); without them the test is skipped.
#
#   ./test/common/metadata_parsers_fuzz_test.sh [--asan] [--strict] [--seed N]
#       [--iterations N] [--only GROUP:N]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer (meant
# to be run this way too; a sanitizer report fails the input).
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS (arm64), also with --asan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=metadata_parsers_fuzz_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

XIPH_PREFIX=""
for prefix in /opt/homebrew /usr/local /usr; do
    if [ -f "$prefix/include/FLAC/stream_decoder.h" ] && [ -f "$prefix/include/ogg/ogg.h" ] &&
       [ -f "$prefix/include/opus/opus.h" ] && [ -f "$prefix/include/vorbis/codec.h" ]; then
        XIPH_PREFIX="$prefix"
        break
    fi
done
if [ -z "$XIPH_PREFIX" ]; then
    echo "SKIPPED: Xiph libraries not found (brew install opus libogg libvorbis flac)"
    exit 0
fi

# The stream decoder set, as in src/CMakeLists.txt; the AAC backend is
# compiled per platform.
if [ "$(uname)" == "Darwin" ]; then
    PLATFORM=(src/audiobuffer/aac_stream_decoder_apple.mm
              -framework AudioToolbox -framework CoreFoundation)
else
    PLATFORM=(src/audiobuffer/aac_stream_decoder_linux.cpp -ldl)
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -I src -I src/soloud/include -I src/soloud/src -I src/native_decoder \
    -I "$XIPH_PREFIX/include" \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/audiobuffer/stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/audiobuffer/wav_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/opus_stream_decoder.cpp \
    src/audiobuffer/vorbis_stream_decoder.cpp \
    src/audiobuffer/flac_stream_decoder.cpp \
    src/audiobuffer/ogg_flac_stream_decoder.cpp \
    src/native_stream/m4a_gapless.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    "${PLATFORM[@]}" \
    -L "$XIPH_PREFIX/lib" -lFLAC -lvorbis -logg -lopus -lm

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

"$OUT" example/assets/audio "$@"

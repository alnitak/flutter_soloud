#!/bin/bash
# Build and run the robustness (fuzz-style) tests of all stream decoders
# (test/common/stream_decoders_fuzz_test.cpp): random bytes, random bytes
# behind each format's magic, and real files truncated and corrupted at random,
# fed through StreamDecoder as BufferStream does and to each wrapper directly.
# Every input runs in a child process with a time limit: no crash, no hang,
# no sanitizer report, clean errors. The seed is fixed, so runs are
# reproducible. Meant to be run with --asan too.
#
# Needs no audio device. Uses the files in example/assets/audio. Needs the
# Xiph libraries (brew install opus libogg libvorbis flac; apt install
# libogg-dev libvorbis-dev libopus-dev libflac-dev); without them the test is
# skipped.
#
#   ./test/common/stream_decoders_fuzz_test.sh [--asan] [--strict] [--seed N]
#                                              [--iterations N] [--only N]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict makes the known never-ending drains fail the run. --only N runs
# input N in-process (see the FAIL line for the number).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=stream_decoders_fuzz_test
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
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    "${PLATFORM[@]}" \
    -L "$XIPH_PREFIX/lib" -lFLAC -lvorbis -logg -lopus -lm

# ASan: a crash is reported by the child; keep reports short and the
# children quick to start.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=0}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

"$OUT" example/assets/audio "$@"

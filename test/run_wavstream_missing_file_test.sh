#!/bin/bash
# Build and run the regression test for a voice of a disk-streamed WavStream
# whose file was deleted, moved or broken after it was loaded (see
# test/wavstream_missing_file_test.cpp). Uses the null backend, so no audio
# hardware is needed.
#
# Ogg Vorbis needs the Xiph libraries; they are linked from the system (brew:
# libogg libvorbis opus flac). Without all of them, or with NO_XIPH=1, it
# builds without them.
#
# Pass --asan to build under AddressSanitizer and UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/wavstream_missing_file_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

# What mb_ogg.cpp includes, and the libraries it needs.
XIPH_HEADERS=(ogg/ogg.h vorbis/codec.h vorbis/vorbisfile.h opus/opus.h FLAC/stream_decoder.h)
XIPH_LINK=(vorbisfile vorbis ogg opus FLAC)

# Sets XIPH_PREFIX to the first prefix with every header and library, so a
# partial install falls back to NO_XIPH_LIBS instead of failing to build;
# XIPH_MISSING says what each partial install lacks.
find_xiph() {
    XIPH_PREFIX=""
    XIPH_MISSING=""
    local prefix header lib missing
    for prefix in /opt/homebrew /usr/local /usr; do
        missing=()
        for header in "${XIPH_HEADERS[@]}"; do
            [ -f "$prefix/include/$header" ] || missing+=("$header")
        done
        [ ${#missing[@]} -eq ${#XIPH_HEADERS[@]} ] && continue
        for lib in "${XIPH_LINK[@]}"; do
            printf 'int main() { return 0; }\n' |
                c++ -x c++ - -o /dev/null -L "$prefix/lib" "-l$lib" 2>/dev/null ||
                missing+=("lib$lib")
        done
        if [ ${#missing[@]} -eq 0 ]; then
            XIPH_PREFIX="$prefix"
            return 0
        fi
        XIPH_MISSING="$XIPH_MISSING $prefix lacks ${missing[*]};"
    done
    return 1
}

XIPH_FLAGS=()
XIPH_LIBS=()
if [ "${NO_XIPH:-0}" == "1" ]; then
    XIPH_FLAGS=(-DNO_XIPH_LIBS)
elif find_xiph; then
    XIPH_FLAGS=(-I "$XIPH_PREFIX/include")
    XIPH_LIBS=(-L "$XIPH_PREFIX/lib")
    for lib in "${XIPH_LINK[@]}"; do
        XIPH_LIBS+=("-l$lib")
    done
else
    echo "Xiph libraries not found;${XIPH_MISSING} building with NO_XIPH_LIBS" >&2
    XIPH_FLAGS=(-DNO_XIPH_LIBS)
fi

# dr_mp3's implementation is compiled in src/audiobuffer/mp3_stream_decoder.cpp,
# like in the plugin.
c++ -std=c++17 -Wall -Wextra -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    "${XIPH_FLAGS[@]}" \
    -I src/soloud/include \
    -I src/soloud/src/audiosource/wav \
    -o "$OUT" \
    test/wavstream_missing_file_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wavstream.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/soloud/src/audiosource/wav/mb_ogg.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    "${XIPH_LIBS[@]}"

"$OUT"

#!/bin/bash
# Build and run the regression test for chained Ogg files whose links differ
# (see test/ogg_chain_test.cpp). Uses the null backend, so no audio hardware
# is needed.
#
# The test builds its files with the Xiph encoders, so it needs the Xiph
# libraries and headers from the system (brew: libogg libvorbis opus flac;
# apt: libogg-dev libvorbis-dev libopus-dev libflac-dev).
#
# Pass --asan to build under AddressSanitizer and UndefinedBehaviorSanitizer.
# Any further arguments name the test groups to run (default: all).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/ogg_chain_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

# What the test and mb_ogg.cpp include, and the libraries they need.
XIPH_HEADERS=(ogg/ogg.h vorbis/codec.h vorbis/vorbisfile.h vorbis/vorbisenc.h opus/opus.h
              FLAC/stream_decoder.h)
XIPH_LINK=(vorbisenc vorbisfile vorbis ogg opus FLAC)

# Sets XIPH_PREFIX to the first prefix with every header and library, so a
# partial install gets a clear message instead of a compile or link error;
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

if ! find_xiph; then
    echo "Xiph libraries not found (libogg, libvorbis, libopus, libFLAC):${XIPH_MISSING%;}" >&2
    exit 1
fi
XIPH_LIBS=()
for lib in "${XIPH_LINK[@]}"; do
    XIPH_LIBS+=("-l$lib")
done

# dr_mp3's implementation is compiled in src/audiobuffer/mp3_stream_decoder.cpp,
# like in the plugin.
c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -I "$XIPH_PREFIX/include" \
    -I src/soloud/include \
    -I src/native_decoder \
    -o "$OUT" \
    test/ogg_chain_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wav.cpp \
    src/soloud/src/audiosource/wav/soloud_wavstream.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/soloud/src/audiosource/wav/mb_ogg.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/soloud/src/backend/null/soloud_null.cpp \
    -L "$XIPH_PREFIX/lib" "${XIPH_LIBS[@]}"

"$OUT" "$@"

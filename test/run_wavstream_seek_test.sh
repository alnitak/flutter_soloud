#!/bin/bash
# Build and run the regression test for seeking a disk-streamed FLAC file
# (see test/wavstream_seek_test.cpp). Uses the null backend, so no audio
# hardware is needed. FLAC streams through dr_flac, so it builds without
# the Xiph libraries.
#
# Pass --asan to build under AddressSanitizer and UndefinedBehaviorSanitizer.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/wavstream_seek_test"
if [ "$1" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
fi

# dr_mp3's implementation is compiled in src/audiobuffer/mp3_stream_decoder.cpp,
# like in the plugin.
c++ -std=c++17 -Wall -Wextra -pthread "${SANITIZER[@]}" \
    -DWITH_NULL \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src/soloud/src/audiosource/wav \
    -o "$OUT" \
    test/wavstream_seek_test.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/audiosource/wav/soloud_wavstream.cpp \
    src/soloud/src/audiosource/wav/dr_impl.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp \
    src/soloud/src/backend/null/soloud_null.cpp

"$OUT"

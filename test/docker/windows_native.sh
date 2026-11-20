#!/bin/bash
# Build the test/windows native tests with mingw-w64 and run them under Wine.
# Mirrors the source/define lists of test/windows/*_test.ps1, which use MSVC
# on real Windows. Run inside the windows container (see run.sh).
# The VST3 test is left out: it needs the VST3 SDK and a GUI session.

set -uo pipefail
cd "$(dirname "$0")/../.."

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc
OUT=/tmp/win_tests
mkdir -p "$OUT"
FLAGS=(-std=c++17 -O2 -w -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -static -static-libgcc -static-libstdc++)
PASS=0; FAIL=0; SKIP=0

run() {
    local name="$1"; shift
    echo "--- $name"
    local log="$OUT/$name.log"
    # Wine's Media Foundation can hang for good (seen on truncated M4A
    # files in os_decoder_windows_test), so each test gets a time limit.
    if timeout "${WINE_TEST_TIMEOUT:-1800}" wine "$@" > "$log" 2>&1; then
        if grep -qE '^SKIPPED' "$log"; then SKIP=$((SKIP+1)); echo "$name: SKIPPED"; else PASS=$((PASS+1)); echo "$name: PASS"; fi
    else
        FAIL=$((FAIL+1)); echo "$name: FAIL"; tail -40 "$log" | sed 's/^/    /'
    fi
}
build() {
    local name="$1"; shift
    if ! "$CXX" "${FLAGS[@]}" "$@" -o "$OUT/$name.exe" > "$OUT/$name.build.log" 2>&1; then
        FAIL=$((FAIL+1)); echo "$name: BUILD FAILED"; head -40 "$OUT/$name.build.log" | sed 's/^/    /'
        return 1
    fi
}

MF_LIBS=(-lmfplat -lmfreadwrite -lmfuuid -lshlwapi -lole32 -luuid -lpropsys)


"$CC" -O2 -w -c -I src/pffft -o "$OUT/pffft.o" src/pffft/pffft.c

# soloud_common.cpp calls msvcrt's _vsprintf_p without including <stdio.h>
# itself (MSVC's headers pull it in); mingw-w64 declares it in
# sec_api/stdio_s.h, reached through <stdio.h>.
echo '#include <stdio.h>' > "$OUT/vsprintf_p_compat.h"
if build change_device_test -DWITH_MINIAUDIO -DNO_XIPH_LIBS -include "$OUT/vsprintf_p_compat.h" \
    -I src/soloud/include -I src -I src/pffft \
    test/common/change_device_test.cpp src/analyzer.cpp src/mixeroutput/*.cpp \
    "$OUT/pffft.o" src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/soloud/src/core/*.cpp src/soloud_common.cpp \
    -lole32 -luser32 -ladvapi32 -lwinmm; then
    run change_device_test "$OUT/change_device_test.exe"
fi

if build native_decoder_test \
    -I src -I src/soloud/include -I src/native_decoder -I src/audiobuffer -I src/soloud/src/audiosource/wav \
    test/common/native_decoder_test.cpp src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_windows.cpp src/audiobuffer/m4a_metadata.cpp \
    src/audiobuffer/mp3_stream_decoder.cpp src/native_decoder/os_decoder_windows.cpp \
    src/native_decoder/native_audio_decoder.cpp \
    "${MF_LIBS[@]}"; then
    run native_decoder_test "$OUT/native_decoder_test.exe"
fi

if build os_decoder_windows_test \
    -I src -I src/native_decoder -I src/audiobuffer \
    test/windows/os_decoder_windows_test.cpp src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_windows.cpp src/audiobuffer/m4a_metadata.cpp \
    src/native_decoder/os_decoder_windows.cpp \
    "${MF_LIBS[@]}"; then
    mkdir -p "$OUT/work"
    run os_decoder_windows_test "$OUT/os_decoder_windows_test.exe" example/assets/audio "$OUT/work"
fi

echo "WINDOWS_NATIVE_SUMMARY: passed=$PASS failed=$FAIL skipped=$SKIP"
[ "$FAIL" -eq 0 ]

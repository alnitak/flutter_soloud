#!/bin/bash
# Build the test/windows native tests with mingw-w64 and run them under Wine.
# Mirrors the source/define lists of test/windows/*_test.ps1, which use MSVC
# on real Windows. Run inside the windows container (see run.sh).
# vst3_host_test is left out: it needs the MSVC-built VST3 SDK library.
# WIN_NATIVE_FILTER=<regex> limits the run to the matching tests.

set -uo pipefail
cd "$(dirname "$0")/../.."

# Wine maps file names through the Unix locale: without a UTF-8 one,
# non-ASCII paths (unicode_paths_test) cannot be created at all.
export LANG=C.UTF-8 LC_ALL=C.UTF-8

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
    # WIN_NATIVE_FILTER=<regex>: build and run only the matching tests.
    if [ -n "${WIN_NATIVE_FILTER:-}" ] && ! [[ "$name" =~ $WIN_NATIVE_FILTER ]]; then
        return 1
    fi
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
    src/audiobuffer/mp3_stream_decoder.cpp src/native_decoder/os_decoder_windows.cpp src/native_stream/m4a_gapless.cpp \
    src/native_decoder/native_audio_decoder.cpp \
    "${MF_LIBS[@]}"; then
    run native_decoder_test "$OUT/native_decoder_test.exe"
fi

if build os_decoder_windows_test \
    -I src -I src/native_decoder -I src/audiobuffer \
    test/windows/os_decoder_windows_test.cpp src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_windows.cpp src/audiobuffer/m4a_metadata.cpp \
    src/native_decoder/os_decoder_windows.cpp src/native_stream/m4a_gapless.cpp \
    "${MF_LIBS[@]}"; then
    mkdir -p "$OUT/work"
    run os_decoder_windows_test "$OUT/os_decoder_windows_test.exe" example/assets/audio "$OUT/work"
fi

if build aac_stream_decoder_windows_test \
    -I src -I src/native_decoder -I src/audiobuffer \
    test/windows/aac_stream_decoder_windows_test.cpp src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/aac_stream_decoder_windows.cpp src/audiobuffer/m4a_metadata.cpp \
    src/native_decoder/os_decoder_windows.cpp src/native_stream/m4a_gapless.cpp \
    "${MF_LIBS[@]}"; then
    run aac_stream_decoder_windows_test "$OUT/aac_stream_decoder_windows_test.exe" example/assets/audio
fi

# The whole plugin (bindings.cpp down to miniaudio) as a static library, for
# the tests that drive the Dart entry points. Built once, in parallel.
PLUGIN_LIB="$OUT/libplugin.a"
plugin_lib() {
    [ -f "$PLUGIN_LIB" ] && return 0
    local srcs=(src/soloud_common.cpp src/bindings.cpp src/player.cpp src/analyzer.cpp
        src/synth/*.cpp src/waveform/waveform.cpp src/waveform/miniaudio_libvorbis.cpp
        src/audiobuffer/*.cpp src/native_stream/*.cpp src/native_decoder/native_audio_decoder.cpp
        src/native_decoder/os_decoder_windows.cpp src/filters/*.cpp src/mixeroutput/*.cpp
        src/soloud/src/core/*.cpp src/soloud/src/filter/*.cpp
        src/soloud/src/audiosource/{ay,monotone,noise,sfxr,speech,tedsid,vic,vizsn,wav}/*.cpp
        src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp)
    mkdir -p "$OUT/plugin_obj"
    printf '%s\n' "${srcs[@]}" | xargs -P "$(nproc)" -I{} sh -c \
        'o="$0/plugin_obj/$(echo {} | tr / _).o"; '"$CXX"' -std=c++17 -O2 -w -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -DWITH_MINIAUDIO -DNO_XIPH_LIBS -include "$0/vsprintf_p_compat.h" -I src/soloud/include -I src/soloud/src -I src/pffft -I src -I src/native_decoder -I src/audiobuffer -c {} -o "$o" || echo "FAILED {}"' "$OUT" \
        > "$OUT/plugin_lib.build.log" 2>&1
    if grep -q '^FAILED' "$OUT/plugin_lib.build.log"; then
        head -40 "$OUT/plugin_lib.build.log"
        return 1
    fi
    x86_64-w64-mingw32-ar rcs "$PLUGIN_LIB" "$OUT"/plugin_obj/*.o "$OUT/pffft.o"
}
PLUGIN_LIBS=(-lmfplat -lmfreadwrite -lmfuuid -lshlwapi -lole32 -luuid -lpropsys -luser32 -ladvapi32 -lwinmm -loleaut32)

if { [ -z "${WIN_NATIVE_FILTER:-}" ] || [[ wasapi_device_change_test =~ $WIN_NATIVE_FILTER ]] ||
     [[ unicode_paths_test =~ $WIN_NATIVE_FILTER ]]; } && ! plugin_lib; then
    FAIL=$((FAIL+1)); echo "plugin library: BUILD FAILED"
fi

if [ -f "$PLUGIN_LIB" ] && build wasapi_device_change_test -I src -I src/audiobuffer \
    test/windows/wasapi_device_change_test.cpp "$PLUGIN_LIB" "${PLUGIN_LIBS[@]}"; then
    run wasapi_device_change_test "$OUT/wasapi_device_change_test.exe"
    # Wine with its audio driver disabled has no render endpoint at all.
    wine reg add 'HKCU\Software\Wine\Drivers' /v Audio /d '' /f >/dev/null 2>&1
    run wasapi_device_change_test_no_device "$OUT/wasapi_device_change_test.exe" --expect-no-device
    wine reg delete 'HKCU\Software\Wine\Drivers' /v Audio /f >/dev/null 2>&1
fi

if [ -f "$PLUGIN_LIB" ] && build unicode_paths_test -I src -I src/audiobuffer \
    test/windows/unicode_paths_test.cpp "$PLUGIN_LIB" "${PLUGIN_LIBS[@]}"; then
    mkdir -p "$OUT/unicode_work"
    run unicode_paths_test "$OUT/unicode_paths_test.exe" 'Z:\work\example\assets\audio' "Z:${OUT//\//\\}\\unicode_work"
fi

# The VST3 editor window test needs the VST3 SDK headers (VST3_PREBUILD_DIR,
# see test/linux/vst3_host_test.sh) and an X display for Wine's windows. The
# MSVC-only SDK library is replaced by test/windows/vst3_sdk_mingw_stubs.cpp.
VST3_INC="${VST3_PREBUILD_DIR:-/work/.dart_tool/flutter_soloud/vst3/prebuild}/include"
if [ -d "$VST3_INC/pluginterfaces" ]; then
    if build vst3_editor_windows_test -DSOLOUD_ENABLE_VST3 -I src -isystem "$VST3_INC" \
        test/windows/vst3_editor_windows_test.cpp test/windows/vst3_sdk_mingw_stubs.cpp \
        src/vst3/vst3_editor_windows.cpp -lole32 -luser32 -lgdi32 -lshell32; then
        if [ -z "${DISPLAY:-}" ] && command -v Xvfb >/dev/null; then
            Xvfb :97 -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
            XVFB_PID=$!
            export DISPLAY=:97
            sleep 1
        fi
        run vst3_editor_windows_test "$OUT/vst3_editor_windows_test.exe"
        [ -n "${XVFB_PID:-}" ] && kill "$XVFB_PID" 2>/dev/null
    fi
elif [ -z "${WIN_NATIVE_FILTER:-}" ] || [[ vst3_editor_windows_test =~ $WIN_NATIVE_FILTER ]]; then
    echo "vst3_editor_windows_test: SKIPPED (no VST3 SDK headers in $VST3_INC)"
    SKIP=$((SKIP+1))
fi

echo "WINDOWS_NATIVE_SUMMARY: passed=$PASS failed=$FAIL skipped=$SKIP"
[ "$FAIL" -eq 0 ]

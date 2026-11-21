#!/bin/bash
# Linux OS decoders (src/native_decoder/os_decoder_linux.cpp and
# src/audiobuffer/aac_stream_decoder_linux.cpp), which dlopen the system
# FFmpeg. Builds test/linux/os_decoder_linux_test.cpp and runs it twice:
#   1. with FFmpeg hidden (OSDEC_HIDE_FFMPEG=1: the test's own dlopen() refuses
#      libav*), checking the clean-failure path every machine can reach;
#   2. with the system FFmpeg, if libavformat/libavcodec .so.58-.so.63 load,
#      checking decoding against test/support/os_decoder_checks.h.
#
#   test/linux/os_decoder_linux_test.sh [--asan]
#   test/linux/os_decoder_linux_test.sh --macos-ffmpeg[=<lib dir>] [--asan]
#
# Platform: Linux (glibc). Needs: a C++17 toolchain and the fixtures in
# example/assets/audio. FFmpeg runtime libraries are optional (Debian/Ubuntu:
# libavformat61 libavcodec61 or similar; no -dev packages): without them run 2
# is reported as not run. Off Linux prints SKIPPED.
#   --asan          build with AddressSanitizer and UBSan. Leaks allocated
#                   by the system libraries while dlopen() initializes them
#                   (libaom, librsvg, ... pulled in by FFmpeg) are suppressed.
#   --macos-ffmpeg  on macOS only: build patched copies of the two Linux
#                   sources (platform guard -> 1, .so.63 names -> the .63.dylib
#                   files in <lib dir>, default Homebrew's ffmpeg or
#                   ffmpeg-full) and run both passes against them. This checks
#                   the hard-coded FFmpeg struct offsets for that one FFmpeg
#                   version without a Linux machine.
# Known src/ bugs are reported as KNOWN BUG and do not fail the run; set
# OSDEC_STRICT=1 to count them.
# Verification: verified on Linux arm64 in test/docker against FFmpeg 4.4,
# 5.1, 6.1, 7.1 and 8 (lavf/lavc 58-62, `test/docker/run.sh ffmpeg all`),
# also with --asan. The --macos-ffmpeg mode was run on
# macOS arm64 against Homebrew FFmpeg 9.0.1 (lavf/lavc 63).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

MACOS_FFMPEG=""
SANITIZER=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1) ;;
        --macos-ffmpeg) MACOS_FFMPEG=auto ;;
        --macos-ffmpeg=*) MACOS_FFMPEG="${arg#--macos-ffmpeg=}" ;;
        *) echo "unknown argument: $arg"; exit 2 ;;
    esac
done

OUT="${TMPDIR:-/tmp}/os_decoder_linux_test"
[ ${#SANITIZER[@]} -gt 0 ] && OUT="${OUT}_asan"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

TEST_SRC=test/linux/os_decoder_linux_test.cpp
DECODER_SRCS=(src/native_decoder/os_decoder_linux.cpp src/audiobuffer/aac_stream_decoder_linux.cpp)
EXTRA_SRCS=()
LIBS=(-ldl -pthread)

if [ -n "$MACOS_FFMPEG" ]; then
    if [ "$(uname -s)" != "Darwin" ]; then
        echo "--macos-ffmpeg is for macOS hosts"
        exit 2
    fi
    if [ "$MACOS_FFMPEG" == "auto" ]; then
        MACOS_FFMPEG=""
        for d in /opt/homebrew/opt/ffmpeg/lib /opt/homebrew/opt/ffmpeg-full/lib /usr/local/opt/ffmpeg/lib; do
            if [ -e "$d/libavformat.63.dylib" ] && [ -e "$d/libavcodec.63.dylib" ]; then
                MACOS_FFMPEG="$d"
                break
            fi
        done
    fi
    if [ -z "$MACOS_FFMPEG" ] || [ ! -e "$MACOS_FFMPEG/libavformat.63.dylib" ]; then
        echo "SKIPPED: no FFmpeg with libavformat.63.dylib found (brew install ffmpeg)"
        exit 0
    fi
    echo "Simulating Linux on macOS with FFmpeg from $MACOS_FFMPEG"
    PATCHED="$WORK_DIR/patched"
    patch_src() {
        mkdir -p "$PATCHED/$(dirname "$1")"
        sed -e 's|^#if defined(__linux__) && !defined(__ANDROID__)|#if 1|' \
            -e "s|\"libavformat.so.63\"|\"$MACOS_FFMPEG/libavformat.63.dylib\"|g" \
            -e "s|\"libavcodec.so.63\"|\"$MACOS_FFMPEG/libavcodec.63.dylib\"|g" \
            "$1" > "$PATCHED/$1"
    }
    for f in "$TEST_SRC" "${DECODER_SRCS[@]}"; do patch_src "$f"; done
    # Relative includes of the patched copies must still reach the real tree.
    mkdir -p "$PATCHED/test/support"
    cp test/support/os_decoder_checks.h "$PATCHED/test/support/"
    for d in native_decoder audiobuffer; do
        for f in src/$d/*; do [ -e "$PATCHED/$f" ] || ln -s "$REPO_ROOT/$f" "$PATCHED/$f"; done
    done
    ln -s "$REPO_ROOT/src/simd" "$PATCHED/src/simd"
    ln -s "$REPO_ROOT/src/native_stream" "$PATCHED/src/native_stream"
    # aac_stream_decoder.cpp dispatches to the Apple impl on macOS.
    cat > "$WORK_DIR/route.cpp" <<'ROUTE'
#include "audiobuffer/aac_stream_decoder.h"
std::unique_ptr<AACDecoderWrapper::Impl> createLinuxAACDecoderImpl(DetectedType format);
std::unique_ptr<AACDecoderWrapper::Impl> createAppleAACDecoderImpl(DetectedType format) {
    return createLinuxAACDecoderImpl(format);
}
ROUTE
    TEST_SRC="$PATCHED/$TEST_SRC"
    DECODER_SRCS=("$PATCHED/src/native_decoder/os_decoder_linux.cpp" "$PATCHED/src/audiobuffer/aac_stream_decoder_linux.cpp")
    EXTRA_SRCS=("$WORK_DIR/route.cpp")
    LIBS=()
elif [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only (on macOS, --macos-ffmpeg runs it against Homebrew FFmpeg)"
    exit 0
fi

c++ -std=c++17 -O2 -Wall "${SANITIZER[@]}" \
    -I src \
    -I src/native_decoder \
    -I src/audiobuffer \
    -o "$OUT" \
    "$TEST_SRC" \
    "${DECODER_SRCS[@]}" \
    "${EXTRA_SRCS[@]}" \
    src/audiobuffer/aac_stream_decoder.cpp \
    src/audiobuffer/m4a_metadata.cpp \
    src/native_stream/m4a_gapless.cpp \
    "${LIBS[@]}"

mkdir -p "$WORK_DIR/tmp"
if [ ${#SANITIZER[@]} -gt 0 ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
fi
echo "=== pass 1: FFmpeg hidden ==="
OSDEC_HIDE_FFMPEG=1 "$OUT" example/assets/audio "$WORK_DIR/tmp" --expect-no-ffmpeg

echo "=== pass 2: system FFmpeg ==="
# FFmpeg logs to stderr; keep it out of the way unless asked for.
if [ -n "${OSDEC_VERBOSE:-}" ]; then
    "$OUT" example/assets/audio "$WORK_DIR/tmp"
else
    "$OUT" example/assets/audio "$WORK_DIR/tmp" 2> "$WORK_DIR/ffmpeg.log" || {
        status=$?
        tail -40 "$WORK_DIR/ffmpeg.log"
        exit $status
    }
fi

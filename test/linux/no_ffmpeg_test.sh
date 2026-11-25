#!/bin/bash
# The plugin on Linux without FFmpeg: AAC, M4A/MP4, AC-3 and E-AC-3 loads
# (loadMem, loadFile, AAC buffer stream) fail cleanly with a clear error, and
# MP3, WAV and FLAC still load and play. Builds
# test/linux/no_ffmpeg_test.cpp against the whole plugin
# (test/support/plugin_build.sh) with miniaudio's null backend only, so it
# needs no audio device.
#
# FFmpeg is hidden by the test's own dlopen() (NO_FFMPEG_HIDE=1, set here), so
# this runs on any Linux machine; `./test/docker/run.sh ffmpeg none` also runs
# it in an image where FFmpeg is not installed at all.
#
#   test/linux/no_ffmpeg_test.sh [--asan|--tsan] [--strict]
#
# Platform: Linux (glibc). Needs: a C/C++ toolchain and the fixtures in
# example/assets/audio. No audio device.
#   --asan    AddressSanitizer + UBSan;  --tsan  ThreadSanitizer.
#   --strict  checks marked KNOWN BUG fail the run.
# Verification: Linux arm64, Ubuntu 24.04 in test/docker (FFmpeg hidden) and
# the `ffmpeg none` image (no FFmpeg installed).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only"
    exit 0
fi

SANITIZER=()
OUT="${TMPDIR:-/tmp}/no_ffmpeg_test"
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --tsan) SANITIZER=(-fsanitize=thread -g -O1); OUT="${OUT}_tsan" ;;
        --strict) ARGS+=("$arg") ;;
        *) echo "unknown argument: $arg"; exit 2 ;;
    esac
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

. test/support/plugin_build.sh
plugin_build "$OUT" test/linux/no_ffmpeg_test.cpp \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL

if [ ${#SANITIZER[@]} -gt 0 ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
    export TSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/tsan.supp${TSAN_OPTIONS:+ $TSAN_OPTIONS}"
fi
NO_FFMPEG_HIDE=1 "$OUT" example/assets/audio ${ARGS[@]+"${ARGS[@]}"}

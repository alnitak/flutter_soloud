#!/bin/bash
# Output-device swap regression test (miniaudio_changeDevice) on macOS.
#
# Builds test/common/change_device_test.cpp against the real miniaudio backend
# (CoreAudio), since that is where changeDevice lives, and runs it. The same
# source runs on Linux (test/linux/change_device_test.sh) and Windows
# (test/windows/change_device_test.ps1).
#
#   test/macos/change_device_test.sh
#
# Platform: macOS. Needs: Xcode command line tools and an output device; with
# no usable device the test prints SKIPPED and exits 0. No assets, no flags.
# Verification: verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIPPED: macOS only (use test/linux/change_device_test.sh on Linux)"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/change_device_test"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The analyzer the device callback feeds uses pffft, which is C.
cc -std=gnu99 -O2 -c -I src/pffft -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -DWITH_MINIAUDIO \
    -DNO_XIPH_LIBS \
    -I src/soloud/include \
    -I src \
    -o "$OUT" \
    test/common/change_device_test.cpp \
    src/soloud_common.cpp \
    src/analyzer.cpp \
    src/soloud/src/core/*.cpp \
    src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp \
    src/mixeroutput/*.cpp \
    "$WORK_DIR/pffft.o" \
    -ldl -lm \
    -framework AudioToolbox -framework CoreFoundation -framework CoreAudio -framework Foundation

"$OUT"

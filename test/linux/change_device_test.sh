#!/bin/bash
# Output-device swap regression test (miniaudio_changeDevice) on Linux.
#
# Builds test/common/change_device_test.cpp against the real miniaudio backend
# and runs it once per Linux audio backend: auto (ALSA, then PulseAudio, then
# JACK), ALSA, and PulseAudio. miniaudio dlopens libasound/libpulse at run
# time, so no -dev packages are needed to build.
#
#   test/linux/change_device_test.sh [--asan]
#
# Platform: Linux (x86_64/arm64). Needs: a C/C++ toolchain and an output
# device. A backend that cannot open a device (no /dev/snd, no PulseAudio or
# PipeWire-pulse server, headless CI) is reported as skipped; when none can,
# the script prints SKIPPED and exits 0. No assets.
#   --asan  build with AddressSanitizer and UBSan.
# Verification: NOT YET VERIFIED ON LINUX (written on macOS; no Linux host).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/change_device_test_linux"
SANITIZER=()
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    OUT="${OUT}_asan"
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The analyzer the device callback feeds uses pffft, which is C.
cc -std=gnu99 -O2 -c -I src/pffft -o "$WORK_DIR/pffft.o" src/pffft/pffft.c

c++ -std=c++17 -O2 -Wall -Wextra -pthread "${SANITIZER[@]}" \
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
    -ldl -lm -lpthread

RAN=0
FAILED=0
SKIPPED_BACKENDS=()
for backend in auto alsa pulse; do
    echo "=== backend: $backend ==="
    log="$WORK_DIR/$backend.log"
    if "$OUT" --linux-backend "$backend" > "$log" 2>&1; then
        status=0
    else
        status=$?
    fi
    if [ $status -eq 0 ] && grep -q '^SKIPPED' "$log"; then
        # Keep the word out of the output unless everything skipped, so the
        # runner does not count a partial run as skipped.
        sed 's/^SKIPPED:/no device:/' "$log"
        SKIPPED_BACKENDS+=("$backend")
        continue
    fi
    cat "$log"
    RAN=$((RAN + 1))
    [ $status -eq 0 ] || FAILED=$((FAILED + 1))
done

if [ $RAN -eq 0 ]; then
    echo "SKIPPED: no Linux audio backend could open an output device"
    exit 0
fi
echo "ran on $RAN backend(s), $FAILED failed; no device on: ${SKIPPED_BACKENDS[*]:-none}"
[ $FAILED -eq 0 ]

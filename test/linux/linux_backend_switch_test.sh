#!/bin/bash
# Switching the Linux audio backend while the engine plays
# (setLinuxAudioBackend -> Player::setLinuxAudioBackend ->
# miniaudio_changeLinuxBackend_impl): ALSA <-> PulseAudio <-> Auto with a live
# voice, a backend that is not available (JACK without libjack), recovery,
# rapid switches. Builds test/linux/linux_backend_switch_test.cpp against the
# whole plugin (test/support/plugin_build.sh).
#
#   test/linux/linux_backend_switch_test.sh [--asan|--tsan] [--strict] [--verbose]
#
# Platform: Linux. Needs: a C/C++ toolchain and a PulseAudio server whose
# device ALSA can also reach (test/docker sets ALSA's default PCM to pulse);
# prints SKIPPED without one, and off Linux. No assets.
#   --asan    AddressSanitizer + UBSan;  --tsan  ThreadSanitizer.
#   --strict  known src/ bugs (KNOWN BUG checks) fail the run.
# Verification: Linux arm64, Ubuntu 24.04 in test/docker (PulseAudio null
# sink), also with --asan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only"
    exit 0
fi

SANITIZER=()
OUT="${TMPDIR:-/tmp}/linux_backend_switch_test"
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --tsan) SANITIZER=(-fsanitize=thread -g -O1); OUT="${OUT}_tsan" ;;
        --strict|--verbose) ARGS+=("$arg") ;;
        *) echo "unknown argument: $arg"; exit 2 ;;
    esac
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

. test/support/plugin_build.sh
PLUGIN_BUILD_NO_COMMON=1 plugin_build "$OUT" test/linux/linux_backend_switch_test.cpp

if [ ${#SANITIZER[@]} -gt 0 ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
    export TSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/tsan.supp${TSAN_OPTIONS:+ $TSAN_OPTIONS}"
fi
"$OUT" ${ARGS[@]+"${ARGS[@]}"}

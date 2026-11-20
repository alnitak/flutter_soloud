#!/bin/bash
# Randomized soak of the whole native engine (bindings.cpp, player.cpp,
# SoLoud with miniaudio's null backend): play/stop/seek/pause/load/dispose,
# filters on sounds, buses and the global chain, bus create/destroy, from a
# fixed seed. Checks at clean checkpoints that heap use does not grow, no
# voices are left over and the engine clock follows wall time. Builds
# test/common/stress_soak_test.cpp (test/support/plugin_build.sh).
#
#   test/common/stress_soak_test.sh [--asan|--tsan] [--seconds N] [--seed S] [--only lpvfdba] [--verbose]
#   SOLOUD_STRESS_SECONDS=600 test/common/stress_soak_test.sh    # the 10 minute soak
#
# Platform: macOS, Linux. No audio device (null backend), no assets. Default
# 10 s; the long run is opt-in through SOLOUD_STRESS_SECONDS or --seconds.
#   --asan  AddressSanitizer + UBSan (heap measured as RSS);
#   --tsan  ThreadSanitizer.
# Verification: Linux arm64 (test/docker), 10 s and 600 s, also with --asan
# and --tsan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/stress_soak_test"
ARGS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --asan) SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1); OUT="${OUT}_asan" ;;
        --tsan) SANITIZER=(-fsanitize=thread -g -O1); OUT="${OUT}_tsan" ;;
        --seconds|--seed|--only) ARGS+=("$1" "$2"); shift ;;
        --verbose) ARGS+=("$1") ;;
        *) echo "unknown argument: $1"; exit 2 ;;
    esac
    shift
done

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

. test/support/plugin_build.sh
plugin_build "$OUT" test/common/stress_soak_test.cpp \
    -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS -DMA_ENABLE_NULL

if [ ${#SANITIZER[@]} -gt 0 ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
    export TSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/tsan.supp${TSAN_OPTIONS:+ $TSAN_OPTIONS}"
fi
"$OUT" ${ARGS[@]+"${ARGS[@]}"}

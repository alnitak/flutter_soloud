#!/bin/bash
# Build and run the tests of AAudioTimestampLatency
# (src/soloud/src/backend/miniaudio/aaudio_timestamp_latency.h), the helper
# behind getHardwareOutputLatency() on Android (AAudio). The result must not
# drift when the timestamp's framePosition advances at a different rate than
# framesWritten (replayed Samsung SM-G991N readings, simulated slow and
# slightly slow devices), must be the plain formula when the counters agree,
# must survive the device being stopped and restarted, must restart on a new
# stream, must stay bounded when the first call comes long after the stream
# started, and must keep the last value while the timestamp is unavailable
# after a restart.
#
# Device: none (the helper is header-only and free of platform APIs, so it
# runs on any host). Assets: none.
#
#   ./test/common/aaudio_timestamp_latency_test.sh [--asan]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# Verified on macOS arm64; the helper itself on a Samsung SM-G991N
# (Android 15), a Galaxy S22 (SM-S908B, Android 16) and a Galaxy Note10+
# (SM-N975F, Android 12).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

SANITIZER=()
OUT="${TMPDIR:-/tmp}/aaudio_timestamp_latency_test"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -O2 -Wall -Wextra -pthread "${SANITIZER[@]}" \
    -o "$OUT" \
    test/common/aaudio_timestamp_latency_test.cpp

"$OUT"

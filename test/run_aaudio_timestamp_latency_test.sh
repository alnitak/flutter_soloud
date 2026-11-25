#!/bin/bash
# Build and run the standalone tests of AAudioTimestampLatency, the helper
# that getHardwareOutputLatency() uses on Android: the result must not drift
# when the AAudio timestamp's framePosition advances at a different rate than
# framesWritten, must match the plain formula when they agree, and must
# survive the device being stopped and restarted. Header-only and free of
# platform APIs, so it runs on any host; no audio device or assets needed.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

OUT="${TMPDIR:-/tmp}/aaudio_timestamp_latency_test"

c++ -std=c++17 -O2 -Wall -Wextra -pthread \
    -o "$OUT" \
    test/aaudio_timestamp_latency_test.cpp

"$OUT"

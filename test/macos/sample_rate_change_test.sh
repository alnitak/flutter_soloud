#!/bin/bash
# The output device changes sample rate (44.1 -> 48 -> 96 -> 44.1 kHz) while
# the engine plays (macOS): builds test/macos/sample_rate_change_test.mm with
# the whole engine and runs it. Plays a 1 kHz sine on the BlackHole virtual
# loopback device, records BlackHole's input and checks the pitch and level
# after each change, plus real-time mixing.
#
#   test/macos/sample_rate_change_test.sh [--strict]
#
# Platform: macOS with BlackHole installed (prints SKIPPED otherwise). Changes
# only BlackHole's nominal rate, and restores it afterwards, also on failure
# or a crash (not on SIGKILL). No real output device or default is touched.
# Recording BlackHole needs the microphone permission for the terminal; without
# it the pitch checks are skipped (a control tone detects this).
# Verification: run on macOS arm64 with BlackHole 16ch; there the agent's
# shell could not record, so the pitch checks were skipped.

set -e
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"
if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIPPED: macOS only"
    exit 0
fi
. test/support/macos_engine_test.sh
macos_engine_test sample_rate_change_test "$@"

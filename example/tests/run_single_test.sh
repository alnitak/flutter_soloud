#!/bin/bash
# Run a single web test and report PASS/FAIL by capturing the browser
# console via CDP (flutter run does not forward wasm web app prints).
# Usage: run_single_test.sh <index> <mt|st> [timeout_seconds]
INDEX="$1"
MODE="$2"
TIMEOUT="${3:-240}"

# flutter run must execute from the example root (tests/run_tests.dart).
cd "$(dirname "$0")/.."

EXTRA=""
if [ "$MODE" = "mt" ]; then
    EXTRA="--wasm"
fi

LOG=$(mktemp "/tmp/soloud_flutter_${MODE}_${INDEX}_XXXXXX")
CDP_LOG="/tmp/soloud_cdp_${MODE}_${INDEX}.log"
: > "$CDP_LOG"

flutter run -d chrome -t tests/run_tests.dart --debug $EXTRA \
    --web-browser-flag="--autoplay-policy=no-user-gesture-required" \
    --dart-define=TEST_ARG="$INDEX" > "$LOG" 2>&1 &
FLUTTER_PID=$!

# Wait for Chrome with a remote-debugging-port owned by flutter_tools.
DEBUG_PORT=""
for _ in $(seq 1 90); do
    DEBUG_PORT=$(ps aux | grep "[f]lutter_tools_chrome_device" \
        | grep -o "remote-debugging-port=[0-9]*" | head -1 | cut -d= -f2)
    [ -n "$DEBUG_PORT" ] && break
    if ! kill -0 "$FLUTTER_PID" 2>/dev/null; then
        echo "RESULT: CRASHED (flutter run exited; log: $LOG)"
        tail -20 "$LOG"
        exit 3
    fi
    sleep 1
done

if [ -z "$DEBUG_PORT" ]; then
    echo "RESULT: CRASHED (no chrome debug port; log: $LOG)"
    kill "$FLUTTER_PID" 2>/dev/null
    exit 3
fi

# Attach CDP listener (reloads the page and captures console output).
"$(dirname "$0")/../../.venv/bin/python" "$(dirname "$0")/cdp_console.py" \
    "$DEBUG_PORT" "localhost" "$TIMEOUT" > "$CDP_LOG" 2>&1
RC=$?

# Cleanup: quit flutter run and its Chrome.
kill "$FLUTTER_PID" 2>/dev/null
sleep 1
kill -9 "$FLUTTER_PID" 2>/dev/null
pkill -f "flutter_tools_chrome_device" 2>/dev/null

case $RC in
    0) echo "RESULT: PASS";;
    1) echo "RESULT: FAIL";;
    *) echo "RESULT: TIMEOUT/ERROR (rc=$RC)";;
esac
grep -E "TEST_(PASSED|FAILED)|Testing with" "$CDP_LOG" | head -5
echo "(flutter log: $LOG, cdp log: $CDP_LOG)"

#!/bin/bash
# Run an arbitrary test entrypoint and capture the browser console via CDP.
# Usage: run_entrypoint.sh <target.dart> <mt|st> [timeout_seconds]
TARGET="$1"
MODE="$2"
TIMEOUT="${3:-240}"

EXTRA=""
if [ "$MODE" = "mt" ]; then
    EXTRA="--wasm"
fi

# flutter run must execute from the example root.
cd "$(dirname "$0")/.."

LOG=$(mktemp "/tmp/soloud_flutter_ep_${MODE}_XXXXXX")
CDP_LOG="/tmp/soloud_cdp_ep_${MODE}.log"
: > "$CDP_LOG"

flutter run -d chrome -t "tests/$TARGET" --debug $EXTRA \
    --web-browser-flag="--autoplay-policy=no-user-gesture-required" \
    > "$LOG" 2>&1 &
FLUTTER_PID=$!

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

"$(dirname "$0")/../../.venv/bin/python" "$(dirname "$0")/cdp_console.py" \
    "$DEBUG_PORT" "localhost" "$TIMEOUT" > "$CDP_LOG" 2>&1

kill "$FLUTTER_PID" 2>/dev/null
sleep 1
kill -9 "$FLUTTER_PID" 2>/dev/null
pkill -f "flutter_tools_chrome_device" 2>/dev/null

grep -E "TEST_(PASSED|FAILED)|REPRO_DONE|STARTING" "$CDP_LOG"
echo "(flutter log: $LOG, cdp log: $CDP_LOG)"

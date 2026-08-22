#!/bin/bash
# Run all web tests sequentially for one flavor and record results.
# Usage: run_all_tests.sh <mt|st> [start_index] [end_index]
MODE="$1"
START="${2:-0}"
END="${3:-48}"
RESULTS="/tmp/soloud_results_${MODE}.txt"
: > "$RESULTS"

cd "$(dirname "$0")"

for i in $(seq "$START" "$END"); do
    echo "=== [$MODE] test $i starting at $(date +%H:%M:%S) ===" | tee -a "$RESULTS"
    OUT=$(./run_single_test.sh "$i" "$MODE" 300 2>&1)
    echo "$OUT" | head -2
    RES=$(echo "$OUT" | grep -oE "RESULT: [A-Z/]+" | head -1)
    NAME=$(echo "$OUT" | grep -oE "TEST_(PASSED|FAILED) [A-Za-z0-9_]+" | head -1 | awk '{print $2}')
    echo "$i ${NAME:-?} $RES" >> "$RESULTS"
done

echo "=== [$MODE] DONE at $(date +%H:%M:%S) ===" | tee -a "$RESULTS"

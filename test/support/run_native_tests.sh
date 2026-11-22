#!/bin/bash
# Run every native test script (`*_test.sh`) in the given test folders and
# print a PASS/FAIL/SKIPPED summary. Used by test/<platform>/all.sh.
#
#   ./test/support/run_native_tests.sh <folder>... [-- extra args for each script]
#
# A script that exits 0 and prints a line starting with SKIPPED (for example change_device_test
# on a machine with no output device) counts as skipped. Set
# NATIVE_TEST_FILTER to a regex to run only matching scripts. Each script's
# output goes to $NATIVE_TEST_LOG_DIR (default: a temp dir) and is shown
# only on failure.

set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

DIRS=()
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
    DIRS+=("$1")
    shift
done
[ "${1:-}" == "--" ] && shift
EXTRA=("$@")

LOG_DIR="${NATIVE_TEST_LOG_DIR:-$(mktemp -d "${TMPDIR:-/tmp}/soloud_native_tests_XXXXXX")}"
mkdir -p "$LOG_DIR"

PASSED=()
FAILED=()
SKIPPED=()

for dir in "${DIRS[@]}"; do
    for script in "$dir"/*_test.sh; do
        [ -e "$script" ] || continue
        name="$(basename "$script" .sh)"
        if [ -n "${NATIVE_TEST_FILTER:-}" ] && ! [[ "$name" =~ $NATIVE_TEST_FILTER ]]; then
            continue
        fi
        log="$LOG_DIR/$(basename "$dir")_$name.log"
        printf '%-45s ' "$dir/$name"
        start=$(date +%s)
        if bash "$script" ${EXTRA[@]+"${EXTRA[@]}"} > "$log" 2>&1; then
            if grep -qE '^SKIPPED(:|$)' "$log"; then
                SKIPPED+=("$dir/$name")
                echo "SKIPPED ($(( $(date +%s) - start ))s)"
            else
                PASSED+=("$dir/$name")
                echo "PASS ($(( $(date +%s) - start ))s)"
            fi
        else
            FAILED+=("$dir/$name")
            echo "FAIL ($(( $(date +%s) - start ))s), log: $log"
            # The failure lines first: they are often above the last 20 lines.
            if grep -q 'FAIL' "$log"; then
                echo "    -- lines with FAIL --"
                grep 'FAIL' "$log" | head -40 | sed 's/^/    /'
                echo "    -- last 20 lines --"
            fi
            tail -20 "$log" | sed 's/^/    /'
        fi
    done
done

echo
echo "NATIVE_SUMMARY: passed=${#PASSED[@]} failed=${#FAILED[@]} skipped=${#SKIPPED[@]} (logs: $LOG_DIR)"
for f in ${FAILED[@]+"${FAILED[@]}"}; do echo "  FAILED: $f"; done
[ ${#FAILED[@]} -eq 0 ]

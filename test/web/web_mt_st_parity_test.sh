#!/bin/bash
# Single-threaded vs multi-threaded WASM build parity: runs
# test/common/web_mt_st_parity_test.dart in Chrome three times through
# test/support/web_tests.sh -- the single-threaded build, the multi-threaded
# build (--wasm, cross-origin isolated), and --wasm with
# flutter_soloud_force_single_threaded (SOLOUD_FORCE_ST=true) -- checks that
# each run loaded the build it should, and compares the measurements the
# test prints (output level and frequency, WAV length, voice-ended delay).
#
#   test/web/web_mt_st_parity_test.sh
#
# Platform: a host with the Flutter SDK and Chrome. To serialize with other
# `flutter test` runs on the same tree, set SOLOUD_TEST_LOCK to a directory
# path: the script holds it (mkdir) while Chrome runs.
# Verification: run on macOS arm64 with Chrome.

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

LOCK="${SOLOUD_TEST_LOCK:-}"
WORK="$(mktemp -d)"
OWN_LOCK=0
release() { if [ "$OWN_LOCK" == "1" ]; then rmdir "$LOCK" 2>/dev/null || true; OWN_LOCK=0; fi; }
cleanup() { rm -rf "$WORK"; release; }
trap cleanup EXIT
TEST=test/common/web_mt_st_parity_test.dart

run() {
    local name="$1"
    shift
    if [ -n "$LOCK" ] && [ -d "$(dirname "$LOCK")" ]; then
        until mkdir "$LOCK" 2>/dev/null; do sleep 3; done
        OWN_LOCK=1
    fi
    local code=0
    env "$@" ./test/support/web_tests.sh ${WASM_FLAG:-} "$TEST" > "$WORK/$name.log" 2>&1 || code=$?
    release
    grep -o 'PARITY {.*}' "$WORK/$name.log" | tail -1 | sed 's/^PARITY //' > "$WORK/$name.json" || true
    grep -o 'flutter_soloud: loading [a-z]* WASM build' "$WORK/$name.log" | tail -1 > "$WORK/$name.flavor" || true
    echo "$name: exit $code, $(cat "$WORK/$name.flavor"), $(cat "$WORK/$name.json")"
    [ "$code" == "0" ] || { tail -30 "$WORK/$name.log"; return 1; }
}

FAIL=0
WASM_FLAG= run st SOLOUD_FORCE_ST= || FAIL=1
WASM_FLAG=--wasm run mt SOLOUD_FORCE_ST= || FAIL=1
WASM_FLAG=--wasm run mt_forced_st SOLOUD_FORCE_ST=true || FAIL=1

python3 - "$WORK" <<'PY' || FAIL=1
import json, sys, os
w = sys.argv[1]
def load(n):
    p = os.path.join(w, n + '.json')
    try:
        return json.load(open(p))
    except Exception:
        return None
def flavor(n):
    try:
        return open(os.path.join(w, n + '.flavor')).read()
    except Exception:
        return ''
ok = True
for name, want in (('st', 'loading st'), ('mt', 'loading mt'), ('mt_forced_st', 'loading st')):
    if want not in flavor(name):
        print(f'FAIL {name}: expected "{want}" build, log says "{flavor(name).strip()}"'); ok = False
st, mt, forced = load('st'), load('mt'), load('mt_forced_st')
if not (st and mt and forced):
    print('FAIL: a run printed no PARITY line'); sys.exit(1)
tol = {'sineRms': 0.02, 'sineHz': 2.0, 'lengthMs': 1.0, 'voiceEndedMs': 300}
for other_name, other in (('mt', mt), ('mt_forced_st', forced)):
    for k, t in tol.items():
        a, b = st.get(k), other.get(k)
        if a is None or b is None or abs(a - b) > t:
            print(f'FAIL {k}: st {a} vs {other_name} {b} (tolerance {t})'); ok = False
        else:
            print(f'ok   {k}: st {a:.3f} vs {other_name} {b:.3f}')
if not mt.get('crossOriginIsolated'):
    print('FAIL mt: the page was not cross-origin isolated'); ok = False
if not forced.get('forceSingleThreaded'):
    print('FAIL mt_forced_st: flutter_soloud_force_single_threaded was not set'); ok = False
sys.exit(0 if ok else 1)
PY

if [ "$FAIL" != "0" ]; then
    echo "MT/ST parity: FAILED"
    exit 1
fi
echo "MT/ST parity: all checks passed."

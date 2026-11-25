#!/bin/bash
# The web engine with a suspended AudioContext (no autoplay permission):
# runs test/common/web_suspended_context_test.dart in Chrome through
# test/support/web_tests.sh, but with a Chrome wrapper that drops the
# --autoplay-policy=no-user-gesture-required flag web_tests.sh adds, and
# with --dart-define=SOLOUD_EXPECT_SUSPENDED=true.
#
#   test/web/web_suspended_context_test.sh [--wasm]
#
# Platform: a host with the Flutter SDK and Chrome. To serialize with other
# `flutter test` runs on the same tree, set SOLOUD_TEST_LOCK to a directory
# path: the script holds it (mkdir) while Chrome runs.
# Verification: run on macOS arm64 with Chrome (single- and multi-threaded).

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

REAL_CHROME="${CHROME_EXECUTABLE:-}"
if [ -z "$REAL_CHROME" ]; then
    for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
             "$(command -v google-chrome || true)" "$(command -v chromium || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && REAL_CHROME="$c" && break
    done
fi
if [ -z "$REAL_CHROME" ]; then
    echo "SKIPPED: Chrome not found"
    exit 0
fi
WRAPPER="$(mktemp -t soloud_chrome_noautoplay.XXXXXX)"
LOCK="${SOLOUD_TEST_LOCK:-}"
OWN_LOCK=0
cleanup() { rm -f "$WRAPPER"; [ "$OWN_LOCK" == "1" ] && rmdir "$LOCK" 2>/dev/null; true; }
cat > "$WRAPPER" <<SH
#!/bin/bash
args=()
for a in "\$@"; do [ "\$a" == "--autoplay-policy=no-user-gesture-required" ] || args+=("\$a"); done
exec "$REAL_CHROME" --autoplay-policy=document-user-activation-required "\${args[@]}"
SH
chmod +x "$WRAPPER"

if [ -n "$LOCK" ] && [ -d "$(dirname "$LOCK")" ]; then
    until mkdir "$LOCK" 2>/dev/null; do sleep 3; done
    OWN_LOCK=1
fi
trap cleanup EXIT
CHROME_EXECUTABLE="$WRAPPER" ./test/support/web_tests.sh "$@" \
    --dart-define=SOLOUD_EXPECT_SUSPENDED=true test/common/web_suspended_context_test.dart

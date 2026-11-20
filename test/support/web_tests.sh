#!/bin/bash
# Run Dart tests in Chrome against the real WASM build in web/.
#
#   ./test/support/web_tests.sh [--wasm] [test paths...]   (default: test/common)
#
# Without --wasm this tests the single-threaded WASM build, with --wasm the
# multi-threaded one (see the end of this script).
#
# The page of a test such as test/common/foo_test.dart is served at
# /common/foo_test.html, and flutter_soloud looks for its scripts at
# assets/packages/flutter_soloud/web/ relative to the page (as in an app).
# The test server only serves test/, so this links that path to web/ for
# the run and removes the link afterwards.

set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

ARGS=()
while [ $# -gt 0 ] && [[ "$1" == --* ]]; do ARGS+=("$1"); shift; done
PATHS=("$@")
[ ${#PATHS[@]} -eq 0 ] && PATHS=(test/common)

LINKS=()
cleanup() { for l in ${LINKS[@]+"${LINKS[@]}"}; do rm -f "$l"; rmdir -p "$(dirname "$l")" 2>/dev/null || true; done; }
trap cleanup EXIT
for p in "${PATHS[@]}"; do
    dir="$p"; [ -f "$dir" ] && dir="$(dirname "$dir")"
    link="$dir/assets/packages/flutter_soloud/web"
    [ -e "$link" ] && continue
    mkdir -p "$(dirname "$link")"
    ln -s "$REPO_ROOT/web" "$link"
    LINKS+=("$link")
done

# The browser build compiles every test file into one program, so a file
# that imports dart:ffi (or the FFI bindings) breaks all of them, even when
# it is marked @TestOn('vm'). Files that import the build hook or bin/
# (outside test/, not served) are VM-only too. Pass only the others.
FFI_IMPORT="^import '(dart:ffi|package:ffi/|package:flutter_soloud/src/bindings/[a-z_]*(_ffi|ffigen)[a-z_]*\.dart|\.\./\.\./(hook|bin)/)"
FILES=()
for p in "${PATHS[@]}"; do
    while IFS= read -r f; do
        grep -qE "$FFI_IMPORT" "$f" || FILES+=("$f")
    done < <(if [ -d "$p" ]; then find "$p" -name '*_test.dart' | sort; else echo "$p"; fi)
done

# Without a user gesture Chrome keeps the AudioContext suspended, so the
# mixer would never run. flutter test takes no browser flags, but it runs
# $CHROME_EXECUTABLE: point it at a wrapper that adds the autoplay flag.
REAL_CHROME="${CHROME_EXECUTABLE:-}"
if [ -z "$REAL_CHROME" ]; then
    for c in "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
             "$(command -v google-chrome || true)" "$(command -v google-chrome-stable || true)" \
             "$(command -v chromium || true)" "$(command -v chromium-browser || true)"; do
        [ -n "$c" ] && [ -x "$c" ] && REAL_CHROME="$c" && break
    done
fi
if [ -n "$REAL_CHROME" ]; then
    WRAPPER="$(mktemp -t soloud_chrome.XXXXXX)"
    LINKS+=("$WRAPPER")
    printf '#!/bin/bash\nexec "%s" --autoplay-policy=no-user-gesture-required "$@"\n' "$REAL_CHROME" > "$WRAPPER"
    chmod +x "$WRAPPER"
    export CHROME_EXECUTABLE="$WRAPPER"
fi

# Without --wasm the page is not cross-origin isolated and the
# single-threaded build is used; with --wasm it is, and the multi-threaded
# build is used. SOLOUD_FORCE_ST=true forces the single-threaded one.
DEFINES=()
[ "${SOLOUD_FORCE_ST:-}" == "true" ] && DEFINES=(--dart-define=SOLOUD_FORCE_ST=true)
flutter test --platform chrome ${DEFINES[@]+"${DEFINES[@]}"} ${ARGS[@]+"${ARGS[@]}"} "${FILES[@]}"

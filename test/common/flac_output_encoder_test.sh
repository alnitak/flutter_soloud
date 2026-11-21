#!/bin/bash
# Build and run the mixer-output FLAC encoder tests
# (test/common/flac_output_encoder_test.cpp): encode -> libFLAC decode
# roundtrip (lossless, exact 16-bit samples), final flush, empty stream,
# lifecycle. No audio device or assets needed. Needs libFLAC (Homebrew
# `flac` on macOS, libflac-dev on Linux); prints SKIPPED if it is missing.
#
#   ./test/common/flac_output_encoder_test.sh [--asan] [scenario]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer. A
# scenario name runs only that scenario. XIPH_PREFIX overrides where libFLAC
# is installed (default: `brew --prefix` on macOS, /usr on Linux).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ -n "${XIPH_PREFIX:-}" ]; then
    XIPH="$XIPH_PREFIX"
elif [ "$(uname)" == "Darwin" ] && command -v brew > /dev/null 2>&1; then
    XIPH="$(brew --prefix)"
elif [ "$(uname)" == "Darwin" ]; then
    XIPH=/usr/local
else
    XIPH=/usr
fi

if [ ! -f "$XIPH/include/FLAC/stream_encoder.h" ]; then
    echo "SKIPPED: libFLAC headers not found under $XIPH/include (set XIPH_PREFIX)"
    exit 0
fi

SANITIZER=()
SUFFIX=""
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    SUFFIX="_asan"
    shift
fi

OUT="${TMPDIR:-/tmp}/flac_output_encoder_test$SUFFIX"

c++ -std=c++17 -O1 -w "${SANITIZER[@]}" \
    -I src \
    -I src/soloud/include \
    -I "$XIPH/include" \
    -o "$OUT" \
    test/common/flac_output_encoder_test.cpp \
    src/mixeroutput/flac_output_encoder.cpp \
    -L "$XIPH/lib" -Wl,-rpath,"$XIPH/lib" -lFLAC

"$OUT" "$@"

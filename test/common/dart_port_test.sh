#!/bin/bash
# Build and run the standalone tests of src/mixeroutput/dart_port.h (see
# test/common/dart_port_test.cpp): resolving Dart_PostInteger from the API
# table and posting capture packet tokens to a native port, with the Dart_*
# functions replaced by fakes. Unresolved post, port 0, closed port: the
# send fails without crashing and the packet is discarded; concurrent
# resolve / send.
#
# Needs no audio device, no assets and no Dart SDK. Links src/mixeroutput
# for the real packet store.
#
#   ./test/common/dart_port_test.sh [--asan|--tsan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer, --tsan
# with ThreadSanitizer. --strict (passed to the test) makes checks marked
# KNOWN BUG fail the run.
#
# Verified on macOS arm64 (plain, --asan, --tsan).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=dart_port_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
elif [ "${1:-}" == "--tsan" ]; then
    SANITIZER=(-fsanitize=thread -g -O1)
    OUT="${OUT}_tsan"
    shift
fi

c++ -std=c++17 -w -pthread "${SANITIZER[@]}" \
    -DNO_XIPH_LIBS \
    -I src -I src/mixeroutput -I src/soloud/include \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/mixeroutput/*.cpp

if [ "${SANITIZER[0]}" == "-fsanitize=thread" ]; then
    export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"
fi
"$OUT" "$@"

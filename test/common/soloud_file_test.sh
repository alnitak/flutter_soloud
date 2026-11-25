#!/bin/bash
# Build and run the unit tests of SoLoud's file classes
# (test/common/soloud_file_test.cpp, src/soloud/src/core/soloud_file.cpp):
# DiskFile and MemoryFile reads and seeks past the end, negative seeks, 0-byte
# files, files deleted or truncated while open, openToMem/openFileToMem
# failures, the Soloud_Filehack stdio replacements.
#
# Needs no audio device and no assets (files made in a temp directory).
#
#   ./test/common/soloud_file_test.sh [--asan] [--strict]
#
# --asan builds with AddressSanitizer and UndefinedBehaviorSanitizer.
# --strict (passed to the test) makes checks marked KNOWN BUG fail the run.
#
# Verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

NAME=soloud_file_test
SANITIZER=(-O2)
OUT="${TMPDIR:-/tmp}/$NAME"
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    OUT="${OUT}_asan"
    shift
fi

c++ -std=c++17 -Wall -Wextra "${SANITIZER[@]}" \
    -I src/soloud/include \
    -o "$OUT" \
    test/common/$NAME.cpp \
    src/soloud/src/core/soloud_file.cpp

"$OUT" "$@"

#!/bin/bash
# Collect line coverage for the Dart tests and the native CMake suite and
# write one LCOV report.
#
#   ./test/support/coverage.sh [--dart-only|--native-only] [-- extra flutter test args]
#
# Output (in coverage/ at the repo root):
#   dart.info     lib/ covered by `flutter test --coverage`
#   native.info   src/ (without the vendored src/soloud and src/pffft) covered
#                 by the native tests in test/CMakeLists.txt
#   lcov.info     both merged
#   html/         HTML report, when genhtml (lcov) is installed
#
# Device tests (tag `device`) are included by default; pass `-- -x device`
# on a machine without an audio output device.
#
# Native coverage uses clang source-based coverage (llvm-profdata/llvm-cov,
# via xcrun on macOS) or, with gcc, gcov through gcovr.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

DO_DART=1
DO_NATIVE=1
case "${1:-}" in
    --dart-only) DO_NATIVE=0; shift ;;
    --native-only) DO_DART=0; shift ;;
esac
[ "${1:-}" == "--" ] && shift
FLUTTER_ARGS=("$@")

OUT_DIR="$REPO_ROOT/coverage"
BUILD_DIR="${COVERAGE_BUILD_DIR:-$REPO_ROOT/build/native_tests_coverage}"
mkdir -p "$OUT_DIR"
rm -f "$OUT_DIR/dart.info" "$OUT_DIR/native.info" "$OUT_DIR/lcov.info"

if [ "$DO_DART" == "1" ]; then
    echo "=== Dart coverage"
    flutter test --coverage --coverage-path "$OUT_DIR/dart_raw.info" \
        test/common test/android test/ios test/linux \
        ${FLUTTER_ARGS[@]+"${FLUTTER_ARGS[@]}"} || echo "(some Dart tests failed; coverage still written)"
    # Keep lib/ only.
    awk '/^SF:/{keep = ($0 ~ /^SF:(.*\/)?lib\//)} keep' \
        "$OUT_DIR/dart_raw.info" > "$OUT_DIR/dart.info"
    rm -f "$OUT_DIR/dart_raw.info"
fi

if [ "$DO_NATIVE" == "1" ]; then
    echo "=== Native coverage"
    if [ "$(uname)" == "Darwin" ]; then
        PROFDATA=(xcrun llvm-profdata)
        LLVMCOV=(xcrun llvm-cov)
        export CC=clang CXX=clang++
    else
        PROFDATA=(llvm-profdata)
        LLVMCOV=(llvm-cov)
    fi
    USE_LLVM=0
    if "${PROFDATA[@]}" --help >/dev/null 2>&1 && \
       [[ "$("${CXX:-c++}" --version 2>&1)" == *clang* ]]; then
        USE_LLVM=1
    fi

    cmake -S test -B "$BUILD_DIR" -DSOLOUD_TEST_COVERAGE=ON \
        -DCMAKE_BUILD_TYPE=Debug >/dev/null
    cmake --build "$BUILD_DIR" --parallel "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

    rm -rf "$BUILD_DIR/profraw"
    mkdir -p "$BUILD_DIR/profraw"
    LLVM_PROFILE_FILE="$BUILD_DIR/profraw/%p-%m.profraw" \
        ctest --test-dir "$BUILD_DIR" -j4 -LE script || echo "(some native tests failed; coverage still written)"

    if [ "$USE_LLVM" == "1" ]; then
        "${PROFDATA[@]}" merge -sparse "$BUILD_DIR"/profraw/*.profraw \
            -o "$BUILD_DIR/merged.profdata"
        # llvm-cov takes the first binary as a positional argument and every
        # other one after -object.
        EXES=()
        while IFS= read -r exe; do
            EXES+=("$exe")
        done < <(find "$BUILD_DIR" -maxdepth 1 -type f -perm -u+x -name '*_test*' | sort)
        if [ "${#EXES[@]}" -eq 0 ]; then
            echo "No instrumented test binaries in $BUILD_DIR: no native report" >&2
            exit 1
        fi
        OBJECTS=()
        for exe in "${EXES[@]:1}"; do
            OBJECTS+=(-object "$exe")
        done
        "${LLVMCOV[@]}" export -format=lcov \
            -instr-profile "$BUILD_DIR/merged.profdata" \
            -ignore-filename-regex='(src/soloud/|src/pffft/|src/filters/signalsmith|test/|/usr/|/opt/|Xcode)' \
            "${EXES[0]}" ${OBJECTS[@]+"${OBJECTS[@]}"} > "$OUT_DIR/native.info"
    elif command -v gcovr >/dev/null 2>&1; then
        gcovr --root "$REPO_ROOT" --object-directory "$BUILD_DIR" \
            --filter 'src/' --exclude 'src/soloud/' --exclude 'src/pffft/' \
            --exclude 'src/filters/signalsmith' \
            --lcov "$OUT_DIR/native.info"
    else
        echo "Neither llvm-profdata nor gcovr found: no native report" >&2
    fi
fi

cat "$OUT_DIR"/dart.info "$OUT_DIR"/native.info 2>/dev/null > "$OUT_DIR/lcov.info" || true

summarize() {
    awk -F: '/^SF:/{f++} /^LF:/{lf+=$2} /^LH:/{lh+=$2}
        END {if (lf) printf "%s: %d files, %d/%d lines (%.1f%%)\n", FILENAME, f, lh, lf, 100*lh/lf}' "$1"
}
for f in "$OUT_DIR"/dart.info "$OUT_DIR"/native.info "$OUT_DIR"/lcov.info; do
    [ -s "$f" ] && summarize "$f"
done

if command -v genhtml >/dev/null 2>&1 && [ -s "$OUT_DIR/lcov.info" ]; then
    genhtml -q --ignore-errors source,unmapped,inconsistent,corrupt,unsupported -o "$OUT_DIR/html" "$OUT_DIR/lcov.info" \
        && echo "HTML report: $OUT_DIR/html/index.html"
fi

#!/usr/bin/env bash
# Build and run the cross-platform SIMD, DSP, alignment, leak-regression,
# and throughput microbenchmark suite on macOS or Linux.
#
# Usage:
#   ./test/native_bench/run_native_bench.sh             # Release (-O3) run
#   ./test/native_bench/run_native_bench.sh --sanitize  # ASan + UBSan run
#   ./test/native_bench/run_native_bench.sh --all       # Both Release & ASan+UBSan

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MODE="${1:---release}"

NPROC=4
if command -v sysctl >/dev/null 2>&1; then
  NPROC="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
elif command -v nproc >/dev/null 2>&1; then
  NPROC="$(nproc 2>/dev/null || echo 4)"
fi

run_build() {
  local build_type="$1"
  local sanitize="$2"
  local tag="$3"
  local build_dir="${TMPDIR:-/tmp}/flutter_soloud_native_bench_${tag}"

  echo "=== Building native_bench_test (${tag}) ==="
  cmake -S "$SCRIPT_DIR" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DENABLE_SANITIZERS="$sanitize"
  cmake --build "$build_dir" --config "$build_type" -j "$NPROC"
  echo "=== Running native_bench_test (${tag}) ==="
  "$build_dir/native_bench_test" "$REPO_ROOT"
}

case "$MODE" in
  --sanitize)
    run_build "RelWithDebInfo" "ON" "sanitize"
    ;;
  --all)
    run_build "Release" "OFF" "release"
    run_build "RelWithDebInfo" "ON" "sanitize"
    ;;
  *)
    run_build "Release" "OFF" "release"
    ;;
esac


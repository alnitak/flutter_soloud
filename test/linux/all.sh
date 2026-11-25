#!/bin/bash
# Run all native tests that apply to linux: test/common plus test/linux.
#   ./test/linux/all.sh [-- --asan|--tsan]
exec "$(dirname "$0")/../support/run_native_tests.sh" test/common test/linux "$@"

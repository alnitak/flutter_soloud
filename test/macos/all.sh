#!/bin/bash
# Run all native tests that apply to macos: test/common plus test/macos.
#   ./test/macos/all.sh [-- --asan|--tsan]
exec "$(dirname "$0")/../support/run_native_tests.sh" test/common test/macos "$@"

#!/bin/bash
# Run all iOS simulator native tests (test/ios/*_test.sh). Each script boots a
# simulator if none is booted and shuts it down again; boot one first
# (xcrun simctl boot <udid>) to share it across scripts.
#   ./test/ios/all.sh [-- --asan]
exec "$(dirname "$0")/../support/run_native_tests.sh" test/ios "$@"

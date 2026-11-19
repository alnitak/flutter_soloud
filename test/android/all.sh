#!/bin/bash
# Run all Android native tests over adb (needs a device or emulator and the
# NDK, see each script's header).
#   ./test/android/all.sh
exec "$(dirname "$0")/../support/run_native_tests.sh" test/android "$@"

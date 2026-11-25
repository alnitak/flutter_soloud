#!/bin/bash
# Helpers for test/ios/*_test.sh: build a command-line test binary for the
# iOS simulator and run it with `xcrun simctl spawn`. Source it, don't run it.
#
#   . test/support/ios_simulator.sh
#   ios_sim_require || exit 0          # prints SKIPPED and returns 1 if no SDK/runtime
#   ios_sim_cxx -o "$OUT" sources...   # xcrun -sdk iphonesimulator clang++ ...
#   ios_sim_acquire                     # sets IOS_SIM_UDID, boots one if needed
#   ios_sim_run "$OUT" args...          # simctl spawn; env via SIMCTL_CHILD_*
#   ios_sim_release                     # shuts down only a simulator we booted
#
# Env: IOS_SIM_UDID picks the simulator (default: a booted one, else the first
# available iPhone, which is then booted and shut down again afterwards).
# IOS_SIM_ARCH (default: the host's, arm64 or x86_64), IOS_SIM_MIN (default
# 15.0). Spawned processes see the host file system, so tests read fixtures
# and write scratch files at host paths.

IOS_SIM_ARCH="${IOS_SIM_ARCH:-$(uname -m)}"
IOS_SIM_MIN="${IOS_SIM_MIN:-15.0}"
IOS_SIM_BOOTED_BY_US=0

ios_sim_require() {
    if [ "$(uname -s)" != "Darwin" ]; then
        echo "SKIPPED: the iOS simulator needs macOS"
        return 1
    fi
    if ! xcrun -sdk iphonesimulator --show-sdk-path > /dev/null 2>&1; then
        echo "SKIPPED: no iphonesimulator SDK (install Xcode)"
        return 1
    fi
    if ! xcrun simctl list runtimes 2>/dev/null | grep -q '^iOS'; then
        echo "SKIPPED: no iOS simulator runtime installed"
        return 1
    fi
    return 0
}

ios_sim_cxx() {
    xcrun -sdk iphonesimulator clang++ -arch "$IOS_SIM_ARCH" -mios-simulator-version-min="$IOS_SIM_MIN" "$@"
}

ios_sim_cc() {
    xcrun -sdk iphonesimulator clang -arch "$IOS_SIM_ARCH" -mios-simulator-version-min="$IOS_SIM_MIN" "$@"
}

ios_sim_acquire() {
    if [ -z "${IOS_SIM_UDID:-}" ]; then
        IOS_SIM_UDID="$(xcrun simctl list devices booted | grep -E -o '\([0-9A-F-]{36}\) \(Booted\)' | head -1 | cut -c2-37)"
    fi
    if [ -z "${IOS_SIM_UDID:-}" ]; then
        IOS_SIM_UDID="$(xcrun simctl list devices available | grep -E 'iPhone' | grep -E -o '\([0-9A-F-]{36}\)' | head -1 | tr -d '()')"
        if [ -z "$IOS_SIM_UDID" ]; then
            echo "SKIPPED: no available iPhone simulator"
            return 1
        fi
    fi
    if ! xcrun simctl list devices booted | grep -q "$IOS_SIM_UDID"; then
        echo "Booting simulator $IOS_SIM_UDID"
        xcrun simctl boot "$IOS_SIM_UDID"
        IOS_SIM_BOOTED_BY_US=1
        xcrun simctl bootstatus "$IOS_SIM_UDID" > /dev/null 2>&1 || true
    fi
    return 0
}

ios_sim_run() {
    xcrun simctl spawn "$IOS_SIM_UDID" "$@"
}

ios_sim_release() {
    if [ "$IOS_SIM_BOOTED_BY_US" == "1" ] && [ -n "${IOS_SIM_UDID:-}" ]; then
        echo "Shutting down simulator $IOS_SIM_UDID"
        xcrun simctl shutdown "$IOS_SIM_UDID" > /dev/null 2>&1 || true
        IOS_SIM_BOOTED_BY_US=0
    fi
}

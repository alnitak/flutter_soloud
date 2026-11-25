// Standalone native tests for the VST3 host (src/vst3/vst3_host.cpp and the
// macOS editor window) against a mock plug-in (test/support/vst3_mock_plugin.cpp).
//
// The tests themselves are shared with Linux and Windows in
// test/support/vst3_host_checks.h (see test/common/vst3_host_test.cpp); this
// file adds what is macOS-only: the exit-time unload is triggered the way a
// terminating app does, by NSApplicationWillTerminateNotification.
//
// Build & run from the flutter_soloud repo root (macOS only):
//
//   ./test/macos/vst3_host_test.sh [--asan|--tsan] [test name]
//
// The editor tests check threading: run them under --tsan.

#include "vst3_host_checks.h"

#import <Cocoa/Cocoa.h>

int main(int argc, char **argv) {
    return vst3test::run(argc, argv, [] {
        [[NSNotificationCenter defaultCenter]
            postNotificationName:NSApplicationWillTerminateNotification
                          object:nil];
    });
}

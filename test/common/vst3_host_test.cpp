// Standalone native tests for the VST3 host (src/vst3/vst3_host.cpp and the
// Linux / Windows editor windows) against a mock plug-in
// (test/support/vst3_mock_plugin.cpp): parameter queues, instrument output,
// editor open/close and out-of-process editor close while the effect
// processes, and the exit-time unload waiting for the audio thread.
//
// The tests are in test/support/vst3_host_checks.h, shared with the macOS
// test (test/macos/vst3_host_test.mm, which triggers the exit-time unload
// through NSApplicationWillTerminateNotification). Here it is
// Vst3HostManager::unloadAllAtExit(), what the manager's destructor runs.
//
// Build & run with test/linux/vst3_host_test.sh or
// test/windows/vst3_host_test.ps1:
//
//   vst3_host_test <directory of the mock bundles> [test name]

#include "vst3_host_checks.h"

int main(int argc, char **argv) {
    return vst3test::run(argc, argv, [] { Vst3HostManager::instance().unloadAllAtExit(); });
}

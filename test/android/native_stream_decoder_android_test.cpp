// On-device tests for the Android streaming M4A/MP4 decoder
// (src/native_stream/native_stream_decoder_android.cpp, LoadMode.disk):
// AMediaExtractor + AMediaCodec. The checks and the fixture manifest are in
// test/support/native_stream_decoder_checks.h.
//
// Build & run with test/android/native_stream_decoder_android_test.sh.

#include "../support/native_stream_decoder_checks.h"

#include <dlfcn.h>

namespace {

/// An app process has a binder thread pool; a bare command-line process does
/// not, and MediaCodec then blocks for good in a binder call (flush/release)
/// once the media server needs to call back (seen after ~10 codecs on an API
/// 34 emulator). Start one the way an app has it (libbinder_ndk, API 29+).
void startBinderThreadPool() {
    void *lib = dlopen("libbinder_ndk.so", RTLD_NOW);
    if (!lib) return;
    using StartFn = void (*)();
    if (auto start = reinterpret_cast<StartFn>(dlsym(lib, "ABinderProcess_startThreadPool"))) start();
}

} // namespace

int main(int argc, char **argv) {
    startBinderThreadPool();
    return runNativeStreamDecoderChecks(argc, argv);
}

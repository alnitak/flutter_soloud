// The streaming M4A/MP4 decoder on iOS (LoadMode.disk):
// src/native_stream/native_stream_decoder_apple.cpp (ExtAudioFile) with
// m4a_gapless.cpp, run in the iOS simulator. test/macos/m4a_gapless_test.cpp
// covers the same backend on macOS only; this runs the shared checks in
// test/support/native_stream_decoder_checks.h (priming, exact length, tone
// onset, seeks, end of data, truncated and unsupported files, pinned
// descriptors) against os_decoder_apple.mm's full decode.
//
// Build & run with test/ios/native_stream_decoder_apple_ios_test.sh.

#include "../support/native_stream_decoder_checks.h"

int main(int argc, char **argv) { return runNativeStreamDecoderChecks(argc, argv); }

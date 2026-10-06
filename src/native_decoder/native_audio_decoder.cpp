#if !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_WIN32) && !defined(_WIN64) && !defined(__linux__) && !defined(__EMSCRIPTEN__)

#include "native_audio_decoder.h"

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    result.errorMessage = "Native audio decoding is not supported on this platform";
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    result.errorMessage = "Native audio decoding is not supported on this platform";
    return result;
}

#endif

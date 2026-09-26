#if defined(__EMSCRIPTEN__)

#include "native_audio_decoder.h"
#include <cstdio>
#include <cstdint>

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    result.errorMessage = "Direct file path decoding is not supported on Web; please use loadMem or setBufferStream";
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    result.errorMessage = "Synchronous decodeMemory is not supported on Web; please use loadMem or setBufferStream";
    return result;
}

#endif // defined(__EMSCRIPTEN__)

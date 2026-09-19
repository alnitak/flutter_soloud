#if defined(__EMSCRIPTEN__)

#include "native_audio_decoder.h"
#include <emscripten.h>
#include <cstdio>
#include <cstdint>

EM_ASYNC_JS(int, js_decode_audio_sync, (const uint8_t *data, int length, float **outPtr, int *outSampleCount, int *outChannels, float *outSampleRate), {
    try {
        var bytes = Module_soloud.HEAPU8.subarray(data, data + length);
        var buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
        var AudioCtx = window.AudioContext || window.webkitAudioContext || window.OfflineAudioContext;
        var ctx = new AudioCtx(1, 44100, 44100);
        var audioBuffer = await ctx.decodeAudioData(buffer);

        var channels = audioBuffer.numberOfChannels;
        var sampleRate = audioBuffer.sampleRate;
        var numSamples = audioBuffer.length;
        var totalFloats = numSamples * channels;

        var pOut = Module_soloud._malloc(totalFloats * 4);
        for (var c = 0; c < channels; c++) {
            var chData = audioBuffer.getChannelData(c);
            Module_soloud.HEAPF32.set(chData, (pOut >> 2) + c * numSamples);
        }

        Module_soloud.setValue(outPtr, pOut, 'i32');
        Module_soloud.setValue(outSampleCount, numSamples, 'i32');
        Module_soloud.setValue(outChannels, channels, 'i32');
        Module_soloud.setValue(outSampleRate, sampleRate, 'float');
        return 0; // Success
    } catch (e) {
        console.error("Web Audio decodeAudioData failed:", e);
        return -1;
    }
});

DecodedAudioData NativeAudioDecoder::decodeFile(const char *filePath) {
    DecodedAudioData result;
    result.errorMessage = "Direct file path decoding is not supported on Web; please use decodeMemory";
    return result;
}

DecodedAudioData NativeAudioDecoder::decodeMemory(const unsigned char *bytes, size_t length) {
    DecodedAudioData result;
    if (!bytes || length == 0) {
        result.errorMessage = "Empty memory buffer";
        return result;
    }

    float *outSamples = nullptr;
    int outSampleCount = 0;
    int outChannels = 0;
    float outSampleRate = 0.0f;

    int res = js_decode_audio_sync(bytes, static_cast<int>(length), &outSamples, &outSampleCount, &outChannels, &outSampleRate);
    if (res != 0 || !outSamples || outSampleCount == 0) {
        result.errorMessage = "Web Audio decodeAudioData failed";
        return result;
    }

    result.samples = outSamples;
    result.sampleCount = static_cast<size_t>(outSampleCount);
    result.channels = static_cast<unsigned int>(outChannels);
    result.sampleRate = outSampleRate;
    result.success = true;
    return result;
}

#endif // defined(__EMSCRIPTEN__)

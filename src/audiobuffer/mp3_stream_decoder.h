#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include "stream_decoder.h"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

#if defined(__APPLE__)
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_ONLY_MP3
#if !defined(__EMSCRIPTEN__)
#define MINIMP3_ONLY_SIMD
#endif
#define MINIMP3_IMPLEMENTATION
#endif
#include "minimp3.h"

/// Wrapper class for MP3 stream decoder using minimp3
class MP3DecoderWrapper : public IDecoderWrapper
{
public:
    MP3DecoderWrapper();

    ~MP3DecoderWrapper();

    bool initializeDecoder(int engineSamplerate, int engineChannels) override;

    void cleanup();

    std::vector<float> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels) override;

    static bool checkForValidFrames(const std::vector<unsigned char>& buffer);

    mp3dec_t decoder;

private:
    bool isInitialized;
    bool validFramesFound;
};

#endif // MP3_STREAM_DECODER_H
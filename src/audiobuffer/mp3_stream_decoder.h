#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

// #define MINIMP3_FLOAT_OUTPUT
// #define MINIMP3_ONLY_MP3
// #if !defined(__EMSCRIPTEN__)
// #define MINIMP3_ONLY_SIMD
// #endif
// #define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

/// Wrapper class for MP3 stream decoder using minimp3
class MP3DecoderWrapper
{
public:
    MP3DecoderWrapper();

    ~MP3DecoderWrapper();

    void cleanup();

    std::vector<float> decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels);

private:
    bool initializeDecoder();

public:
    mp3dec_t decoder;
private:
    bool isInitialized;
    bool validFramesFound;
};

#endif // MP3_STREAM_DECODER_H
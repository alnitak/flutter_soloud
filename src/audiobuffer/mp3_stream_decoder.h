#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

#include "minimp3.h"

/// Wrapper class for MP3 stream decoder using minimp3
class MP3DecoderWrapper
{
public:
    MP3DecoderWrapper(int sampleRate, int channels);

    ~MP3DecoderWrapper();

    void cleanup();

    std::vector<float> decode(std::vector<unsigned char>& buffer);

private:
    bool initializeDecoder();

public:
    mp3dec_t decoder;
private:
    uint32_t targetSampleRate;
    uint32_t targetChannels;
    bool isInitialized;
    bool validFramesFound;
};

#endif // MP3_STREAM_DECODER_H
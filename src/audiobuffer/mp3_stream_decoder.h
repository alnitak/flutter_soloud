#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
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

#include "stream_decoder.h"

/// Wrapper class for MP3 stream decoder using minimp3
class MP3DecoderWrapper : public IDecoderWrapper
{
public:
    MP3DecoderWrapper();

    ~MP3DecoderWrapper();

    bool initializeDecoder(int engineSamplerate, int engineChannels) override;

    // call this only once before decoding
    void setMp3BufferIcyMetaInt(int icyMetaInt);

    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels) override;

    static bool checkForValidFrames(const std::vector<unsigned char>& buffer);

    mp3dec_t decoder;

private:
    bool extractID3Tags(const std::vector<unsigned char>& buffer, AudioMetadata& metadata);
    std::vector<unsigned char> checkIcyMeta(std::vector<unsigned char> &buffer, size_t *bytes_discarded_at_end);
    size_t getLastFrameStartingPos(std::vector<unsigned char> &buffer, size_t *bytes_discarded_at_end);
    std::vector<unsigned char> audioData;
    size_t bytes_until_meta;
    size_t metadata_remaining;
    std::string metadata_buffer;
    std::string lastMetadata;
    int mIcyMetaInt;
    bool validFramesFound;
    bool ID3TagsFound;
};

#endif // MP3_STREAM_DECODER_H
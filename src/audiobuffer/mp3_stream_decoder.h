#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <cstring>
#include <deque>

#include "../soloud/src/audiosource/wav/dr_mp3.h"
#include "stream_decoder.h"

/// Wrapper class for MP3 stream decoder using dr_mp3
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

    drmp3 decoder;

private:
    static size_t on_read(void* pUserData, void* pBufferOut, size_t bytesToRead);
    static drmp3_bool32 on_seek(void* pUserData, int offset, drmp3_seek_origin origin);

    bool extractID3Tags(const std::vector<unsigned char>& buffer, AudioMetadata& metadata);
    std::vector<unsigned char> checkIcyMeta(std::vector<unsigned char> &buffer, size_t *bytes_discarded_at_end);
    size_t getLastFrameStartingPos(std::vector<unsigned char> &buffer, size_t *bytes_discarded_at_end);
    bool isInitialized;
    std::vector<unsigned char> audioData;
    size_t m_read_pos;
    size_t bytes_until_meta;
    std::string metadata_buffer;
    std::string lastMetadata;
    int mIcyMetaInt;
    bool ID3TagsFound;
};

#endif // MP3_STREAM_DECODER_H
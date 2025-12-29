#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <cstring>

#include "../soloud/src/audiosource/wav/dr_mp3.h"
#include "stream_decoder.h"

/// Wrapper class for MP3 stream decoder using minimp3 (low-level API)
/// This avoids the drmp3 high-level API's atEnd state management issues
/// that cause glitches in streaming scenarios.
class MP3DecoderWrapper : public IDecoderWrapper
{
public:
    MP3DecoderWrapper();
    ~MP3DecoderWrapper();

    bool initializeDecoder(int engineSamplerate, int engineChannels) override;

    // call this only once before decoding
    void setIcyMetaInt(int icyMetaInt);

    std::pair<std::vector<float>, DecoderError> decode(
        std::vector<unsigned char>& buffer,
        int* samplerate,
        int* channels) override;

    static bool checkForValidFrames(const std::vector<unsigned char>& buffer);

private:
    // Process ICY stream metadata (for internet radio)
    void processIcyStream(std::vector<unsigned char>& buffer);

    // Parse ID3v2 tags and trigger metadata callback
    void parseID3v2Tags(const unsigned char* data, size_t size);

    // Low-level minimp3 decoder (only 4 bytes of state)
    drmp3dec mp3dec;

    // Input buffer for accumulated MP3 data
    std::vector<unsigned char> inputBuffer;

    // Cached audio format info
    int cachedSampleRate;
    int cachedChannels;

    // ICY metadata handling
    int mIcyMetaInt;
    size_t bytes_until_meta;
    std::string lastMetadata;

    // ID3 tag handling
    bool ID3TagsProcessed;
};

#endif // MP3_STREAM_DECODER_H

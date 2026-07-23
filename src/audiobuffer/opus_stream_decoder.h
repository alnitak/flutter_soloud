#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include "stream_decoder.h"
#include "ogg_seek_index.h"
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdint>

#ifdef __EMSCRIPTEN__
// For Web include dirs downloaded from git for build
#include "../../xiph/opus/include/opus.h"
#include "../../xiph/ogg/include/ogg/ogg.h"
#else
#include <fstream>
#include <deque>
#include <opus/opus.h>
#include <ogg/ogg.h>
#endif

/// Wrapper class for Opus stream decoder
///
/// The supported sampleRate for Opus format are 8, 12, 16, 24 amd 48 KHz.
/// The channels is the number of channels in the audio data. Only 1 or 2 allowed.
class OpusDecoderWrapper : public IDecoderWrapper
{
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();

    
    bool initializeDecoder(int engineSamplerate, int engineChannels) override;
    
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels, size_t maxOutputSamples = 0) override;

    bool canSeekToTime(double seconds) const override;
    uint64_t timeToByteOffset(double seconds) override;
    double getDuration() const override;
    int preSkip() const override { return static_cast<int>(opusInfo.pre_skip); }
    int granuleSampleRate() const override { return 48000; }
    void prepareForSeek(uint64_t targetSample) override;

private:
    void getMetadata(ogg_packet* packet);
    OpusInfo parseOpusHead(ogg_packet *packet);
    void decodePacket(ogg_packet *packet, std::vector<float> &out);
    bool ensureDecoder(int newSampleRate, int newChannels);

    OpusDecoder *decoder;
    int engineSamplerate;
    int engineChannels;
    int decodingSamplerate;
    int decodingChannels;

    // Ogg state variables
    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    bool streamInitialized;

    // Header parsing state
    bool headerParsed;
    int packetCount;
    int skipSamplesPending;
    int64_t totalOutputSamples;
    int64_t totalSamplesExpected;

    OpusInfo opusInfo;

    OggSeekIndex mSeekIndex;

    /// Scratch buffer for opus_decode_float, reused across packets to avoid
    /// allocating per packet.
    std::vector<float> mOutputScratch;
};

#endif // OPUS_STREAM_DECODER_H

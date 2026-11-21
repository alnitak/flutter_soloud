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
#include "../../xiph/opus/include/opus_multistream.h"
#include "../../xiph/ogg/include/ogg/ogg.h"
#else
#include <fstream>
#include <deque>
#if __has_include(<opus/opus.h>)
#include <opus/opus.h>
#include <opus/opus_multistream.h>
#else
#include <opus.h>
#include <opus_multistream.h>
#endif
#include <ogg/ogg.h>
#endif

/// Wrapper class for Opus stream decoder
///
/// The supported sampleRate for Opus format are 8, 12, 16, 24 amd 48 KHz.
/// The output has the stream's channels, at most 2: streams with 3 to 8
/// channels (mapping family 1, Vorbis channel order) are downmixed to stereo
/// with the matrices of RFC 7845 section 5.1.1.5.
class OpusDecoderWrapper : public IDecoderWrapper
{
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();

    
    bool initializeDecoder(int engineSamplerate, int engineChannels) override;
    
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels, size_t maxOutputSamples = 0) override;

    void setDataEnded() override { mDataEnded = true; }
    bool hasPendingData() const override;

    bool canSeekToTime(double seconds) const override;
    uint64_t timeToByteOffset(double seconds) override;
    double getDuration() const override;
    int preSkip() const override { return static_cast<int>(opusInfo.pre_skip); }
    int granuleSampleRate() const override { return 48000; }
    void prepareForSeek(uint64_t targetSample, uint64_t byteOffset) override;

private:
    void getMetadata(ogg_packet* packet);
    OpusInfo parseOpusHead(ogg_packet *packet);
    void decodePacket(ogg_packet *packet, std::vector<float> &out);
    int decodeNextPacket(std::vector<float> &out);
    bool ensureDecoder(int newSampleRate, const OpusInfo &head);
    ogg_int64_t timelineGranule(ogg_int64_t granule) const;
    void resyncAfterSeek(const ogg_page &page);

    OpusMSDecoder *decoder;
    /// Layout the decoder was created for.
    int decoderStreams = 0;
    int decoderCoupled = 0;
    std::vector<unsigned char> decoderMapping;
    /// Mapping family 1: 3-8 channels in Vorbis order, downmixed to stereo.
    bool downmixFamily1 = false;
    /// Channels in the stream (decodingChannels is the output, at most 2).
    int sourceChannels;
    int engineSamplerate;
    int engineChannels;
    int decodingSamplerate;
    int decodingChannels;

    // Ogg state variables
    ogg_sync_state oy;
    ogg_stream_state os;
    ogg_page og;
    ogg_packet op;
    bool syncInitialized;
    bool streamInitialized;

    // Header parsing state
    bool headerParsed;
    int packetCount;
    int skipSamplesPending;
    int64_t totalOutputSamples;
    int64_t totalSamplesExpected;
    bool mDataEnded;
    bool mDrained;

    OpusInfo opusInfo;

    OggSeekIndex mSeekIndex;

    /// Chained streams: the seek index maps a time from the start of the
    /// file (48 kHz units) to a byte offset. Start of the current link on
    /// that timeline, its pre-skip and the last granule seen in it.
    int64_t mLinkStart = 0;
    int mLinkPreSkip = 0;
    int64_t mLinkLastGranule = 0;

    /// After prepareForSeek(): the first page with a granule tells where
    /// decoding resumes, and the samples before [mSeekTarget] (output rate,
    /// from the start of the file) are dropped.
    bool mSeekResync = false;
    int64_t mSeekTarget = 0;

    /// Scratch buffer for opus_multistream_decode_float, reused across packets to avoid
    /// allocating per packet.
    std::vector<float> mOutputScratch;
};

#endif // OPUS_STREAM_DECODER_H

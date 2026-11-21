#ifndef VORBIS_DECODER_H
#define VORBIS_DECODER_H

#include "stream_decoder.h"
#include "ogg_seek_index.h"
#ifdef __EMSCRIPTEN__
// For Web include dirs downloaded from git for build
#include "../../xiph/vorbis/include/vorbis/codec.h"
#include "../../xiph/ogg/include/ogg/ogg.h"
#else
#include <vorbis/codec.h>
#include <ogg/ogg.h>
#endif

class VorbisDecoderWrapper : public IDecoderWrapper {
public:
    VorbisDecoderWrapper();
    ~VorbisDecoderWrapper();

    
    bool initializeDecoder(int engineSamplerate, int engineChannels) override;
    
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels, size_t maxOutputSamples = 0) override;

    void setDataEnded() override { mDataEnded = true; }
    bool hasPendingData() const override;

    bool canSeekToTime(double seconds) const override;
    uint64_t timeToByteOffset(double seconds) override;
    double getDuration() const override;
    int granuleSampleRate() const override { return static_cast<int>(vi.rate); }
    void prepareForSeek(uint64_t targetSample, uint64_t byteOffset) override;

private:
    AudioMetadata getMetadata();
    void decodePacket(ogg_packet* packet, std::vector<float> &out);
    void applyFades(float *dst, int frames, int chs);

    int decodingSamplerate;
    int decodingChannels;

    vorbis_info vi;
    vorbis_comment vc;
    vorbis_dsp_state vd;
    vorbis_block vb;
    // Ogg state variables
    ogg_sync_state oy;
    ogg_packet op;
    ogg_stream_state os;
    ogg_page og;

    bool vorbisInitialized;
    bool headersInitialized;
    bool syncInitialized;
    bool streamInitialized;
    bool headerParsed;
    int packetCount;
    bool mDataEnded;
    bool mDrained;

    OggSeekIndex mSeekIndex;
    ogg_int64_t mTotalSamples = -1;

    /// Chained streams: the seek index maps a time from the start of the
    /// file (in samples) to a byte offset. Start of the current link on that
    /// timeline and the last granule seen in it.
    ogg_int64_t mLinkStart = 0;
    ogg_int64_t mLinkLastGranule = 0;
    /// Frames of the current link returned so far (-1: unknown, after a
    /// seek), and the link's EOS granule (-1: not seen yet).
    ogg_int64_t mLinkFrames = 0;
    ogg_int64_t mLinkEnd = -1;
};

#endif // VORBIS_DECODER_H

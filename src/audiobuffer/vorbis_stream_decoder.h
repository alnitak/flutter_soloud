#ifndef VORBIS_DECODER_H
#define VORBIS_DECODER_H

#include "stream_decoder.h"
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

    bool canSeekToTime(double seconds) const override;
    uint64_t timeToByteOffset(double seconds) override;
    double getDuration() const override;
    int granuleSampleRate() const override { return static_cast<int>(vi.rate); }
    void prepareForSeek(uint64_t targetSample) override;

private:
    AudioMetadata getMetadata();
    std::vector<float> decodePacket(ogg_packet* packet);

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
    bool streamInitialized;
    bool headerParsed;
    int packetCount;

    struct TimeByteOffset {
        ogg_int64_t granule;
        uint64_t byteOffset;
    };
    std::vector<TimeByteOffset> mTimeByteOffsets;
    uint64_t mOggBytesConsumed = 0;
    ogg_int64_t mTotalSamples = -1;
};

#endif // VORBIS_DECODER_H

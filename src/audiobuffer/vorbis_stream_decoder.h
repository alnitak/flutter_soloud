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
    
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels) override;
    
private:
    AudioMetadata getMetadata();
    std::vector<float> decodePacket(ogg_packet* packet);
    
    int engineSamplerate;
    int engineChannels;
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
};

#endif // VORBIS_DECODER_H

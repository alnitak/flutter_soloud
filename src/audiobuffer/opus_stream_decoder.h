#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include "stream_decoder.h"
#include <vector>
#include <iostream>
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
    
    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels) override;
    
private:
    AudioMetadata getMetadata(ogg_packet* packet);
    OpusInfo parseOpusHead(ogg_packet *packet);
    std::vector<float> decodePacket(ogg_packet *packet);
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
};

#endif // OPUS_STREAM_DECODER_H

#ifndef OPUS_STREAM_DECODER_H
#define OPUS_STREAM_DECODER_H

#include "stream_decoder.h"
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>

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
    std::vector<float> decodePacket(const unsigned char *packetData, size_t packetSize);

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
    bool headerParsed{false};
    int packetCount{0};
};

#endif // OPUS_STREAM_DECODER_H
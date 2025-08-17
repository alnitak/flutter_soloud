#if !defined(NO_OPUS_OGG_LIBS)

#include "vorbis_stream_decoder.h"
#include <stdexcept>
#include <cstring>

VorbisDecoderWrapper::VorbisDecoderWrapper()
    : vorbisInitialized(false), streamInitialized(false), headerParsed(false), packetCount(0)
{
}

VorbisDecoderWrapper::~VorbisDecoderWrapper() {
    if (vorbisInitialized) {
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
    }
    vorbis_comment_clear(&vc);
    vorbis_info_clear(&vi);

    if (streamInitialized) {
        ogg_stream_clear(&os);
    }
    ogg_sync_clear(&oy);
}

bool VorbisDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    decodingChannels = engineChannels > 2 ? 2 : (engineChannels<=0 ? 1 : engineChannels);
    // Choose the best sample rate nearest to the engine samplerate
    if (engineSamplerate <= 8000) decodingSamplerate = 8000;
    else if (engineSamplerate <= 12000) decodingSamplerate = 12000;
    else if (engineSamplerate <= 16000) decodingSamplerate = 16000;
    else if (engineSamplerate <= 24000) decodingSamplerate = 24000;
    else if (engineSamplerate <= 48000) decodingSamplerate = 48000;
    else decodingSamplerate = 96000;

    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    ogg_sync_init(&oy);
    return true;
}


std::pair<std::vector<float>, DecoderError> VorbisDecoderWrapper::decode(std::vector<unsigned char>& buffer,
                                               int* samplerate,
                                               int* channels) {
    std::vector<float> decodedData;

    if (buffer.empty()) {
        return {decodedData, DecoderError::NoError};
    }

    // Reset sync for new data
    ogg_sync_reset(&oy);

    // Write new bytes into ogg buffer
    char* oggBuffer = ogg_sync_buffer(&oy, buffer.size());
    memcpy(oggBuffer, buffer.data(), buffer.size());
    ogg_sync_wrote(&oy, buffer.size());

    // Process available pages
    while (ogg_sync_pageout(&oy, &og) == 1) {
        
        if (!streamInitialized) {
            if (ogg_stream_init(&os, ogg_page_serialno(&og))) {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        if (ogg_stream_pagein(&os, &og) < 0) {
            return {decodedData, DecoderError::ErrorReadingPage};
        }

        // Extract packets from page
        while (ogg_stream_packetout(&os, &op) == 1) {
            auto packetData = decodePacket(&op);
            decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
        }
    }

    // Discard processed input
    if (oy.returned > 0) {
        if (buffer.size() >= static_cast<size_t>(oy.returned)) {
            buffer.erase(buffer.begin(), buffer.begin() + oy.returned);
        } else {
            buffer.clear();
        }
    }

    // Return samplerate/channels to caller (once headers are parsed)
    if (headerParsed && samplerate && channels) {
        *samplerate = vi.rate;
        *channels   = vi.channels;
    }

    return {decodedData, DecoderError::NoError};
}


std::vector<float> VorbisDecoderWrapper::decodePacket(ogg_packet* packet) {
    std::vector<float> packetPcm;

    if (!headerParsed) {
        // Header parsing phase (3 packets: identification, comment, setup)
        if (packetCount < 3) {
            if (vorbis_synthesis_headerin(&vi, &vc, packet) < 0) {
                fprintf(stderr, "Error processing Vorbis header %d\n", packetCount);
                return packetPcm;
            }

            packetCount++;
            if (packetCount == 3) {
                // All headers parsed → initialize decoder
                if (vorbis_synthesis_init(&vd, &vi) == 0) {
                    vorbis_block_init(&vd, &vb);
                    vorbisInitialized = true;
                    headerParsed = true;
                }
            }
            return packetPcm; // no PCM yet
        }
    }

    if (!vorbisInitialized) {
        return packetPcm;
    }

    // Decode an audio packet
    if (vorbis_synthesis(&vb, packet) == 0) {
        vorbis_synthesis_blockin(&vd, &vb);
    }

    float** pcm;
    int samples;
    while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
        for (int i = 0; i < samples; i++) {
            for (int ch = 0; ch < vi.channels; ch++) {
                packetPcm.push_back(pcm[ch][i]);
            }
        }
        vorbis_synthesis_read(&vd, samples);
    }

    return packetPcm;
}
#endif // #if defined(NO_OPUS_OGG_LIBS)
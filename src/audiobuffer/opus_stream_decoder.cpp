#if !defined(NO_OPUS_OGG_LIBS)

#include "opus_stream_decoder.h"

OpusDecoderWrapper::OpusDecoderWrapper()
    : streamInitialized(false)
{
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
    if (decoder)
        opus_decoder_destroy(decoder);

    if (streamInitialized)
    {
        ogg_stream_clear(&os);
    }
    ogg_sync_clear(&oy);
}

bool OpusDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    decodingChannels = engineChannels > 2 ? 2 : (engineChannels<=0 ? 1 : engineChannels);
    // Choose the best sample rate nearest to the engine samplerate
    if (engineSamplerate <= 8000) decodingSamplerate = 8000;
    else if (engineSamplerate <= 12000) decodingSamplerate = 12000;
    else if (engineSamplerate <= 16000) decodingSamplerate = 16000;
    else if (engineSamplerate <= 24000) decodingSamplerate = 24000;
    else if (engineSamplerate <= 48000) decodingSamplerate = 48000;
    else decodingSamplerate = 96000;
    int error;
    decoder = opus_decoder_create(decodingSamplerate, decodingChannels, &error);
    if (error != OPUS_OK)
    {
        return false;
    }

    ogg_sync_init(&oy);
    return true;
}

std::pair<std::vector<float>, DecoderError> OpusDecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels)
{
    std::vector<float> decodedData;

    if (buffer.empty())
    {
        return {decodedData, DecoderError::NoError};
    }

    ogg_sync_reset(&oy);

    // Write data into ogg sync buffer
    char *oggBuffer = ogg_sync_buffer(&oy, buffer.size());
    memcpy(oggBuffer, buffer.data(), buffer.size());
    ogg_sync_wrote(&oy, buffer.size());

    // Read and process pages
    while (ogg_sync_pageout(&oy, &og) == 1)
    {
        if (!streamInitialized)
        {
            if (ogg_stream_init(&os, ogg_page_serialno(&og)))
            {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        if (ogg_stream_pagein(&os, &og) < 0)
        {
            return {decodedData, DecoderError::ErrorReadingPage};
        }

        // Extract packets from page
        while (ogg_stream_packetout(&os, &op) == 1)
        {
            auto packetData = decodePacket(op.packet, op.bytes);
            decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
        }
    }

    if (oy.returned > 0)
    {
        if (buffer.size() >= oy.returned)
        {
            buffer.erase(buffer.begin(), buffer.begin() + oy.returned);
        } else {
            buffer.erase(buffer.begin(), buffer.end());
        }
    }

    return {decodedData, DecoderError::NoError};
}

std::vector<float> OpusDecoderWrapper::decodePacket(const unsigned char *packetData, size_t packetSize)
{
    std::vector<float> packetPcm;

    // Skip header packets (first 2 packets in Ogg Opus stream)
    if (!headerParsed)
    {
        // Try to identify if this is an Opus header
        if (packetSize >= 8 && memcmp(packetData, "OpusHead", 8) == 0)
        {
            // This is the Opus header, we could parse it for more info if needed
            headerParsed = false;
        }
        else if (packetSize >= 8 && memcmp(packetData, "OpusTags", 8) == 0)
        {
            // This is the OpusTags packet
            headerParsed = true;
        }
        packetCount++;
        return packetPcm;
    }

    // Opus can handle frame sizes from 2.5ms to 60ms
    // We'll use buffer size to accommodate any frame size
    const int maxFrameSize = decodingSamplerate * 60 / 1000; // 60ms frame size
    std::vector<float> outputBuffer(maxFrameSize * decodingChannels);

    // Try decoding the packet
    int samples = opus_decode_float(decoder,
                                    packetData,
                                    packetSize,
                                    outputBuffer.data(),
                                    maxFrameSize,
                                    0);

    if (samples < 0)
    {
        // const char* errorMsg = opus_strerror(samples);
        // fprintf(stderr, "Opus decode error: %s (code: %d) packet size: %zu\n", 
        //         errorMsg, samples, packetSize);
        
        // Additional debugging info
        // if (packetSize < 1)
        //     fprintf(stderr, "Warning: Empty packet received\n");
        // else
        //     fprintf(stderr, "Packet starts with: %02x %02x %02x %02x\n",
        //             packetData[0], packetData[1], 
        //             packetData[2], packetData[3]);
        
        return packetPcm; // Skip invalid packet instead of throwing
    }

    if (samples > 0)
    {
        packetPcm.insert(packetPcm.end(),
                         outputBuffer.begin(),
                         outputBuffer.begin() + samples * decodingChannels);
    }
    return packetPcm;
}
#endif // #if defined(NO_OPUS_OGG_LIBS)
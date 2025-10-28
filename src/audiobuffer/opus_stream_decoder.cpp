#if !defined(NO_OPUS_OGG_LIBS)

#include "opus_stream_decoder.h"
#include <algorithm>

OpusDecoderWrapper::OpusDecoderWrapper()
    : decoder(nullptr),
      engineSamplerate(48000),
      engineChannels(2),
      decodingSamplerate(48000),
      decodingChannels(2),
      streamInitialized(false),
      headerParsed(false),
      packetCount(0),
      skipSamplesPending(0),
      totalOutputSamples(0),
      totalSamplesExpected(-1)
{
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
    if (decoder)
    {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }

    if (streamInitialized)
    {
        ogg_stream_clear(&os);
    }
    ogg_sync_clear(&oy);
}

bool OpusDecoderWrapper::ensureDecoder(int newSampleRate, int newChannels)
{
    if (newSampleRate <= 0)
        newSampleRate = 48000;
    if (newSampleRate < 8000)
        newSampleRate = 8000;
    if (newSampleRate > 48000)
        newSampleRate = 48000;

    if (newChannels <= 0)
        newChannels = 1;
    if (newChannels > 2)
        newChannels = 2;

    if (decoder &&
        newSampleRate == decodingSamplerate &&
        newChannels == decodingChannels)
    {
        opus_decoder_ctl(decoder, OPUS_RESET_STATE);
        return true;
    }

    if (decoder)
    {
        opus_decoder_destroy(decoder);
        decoder = nullptr;
    }

    int error = OPUS_OK;
    decoder = opus_decoder_create(newSampleRate, newChannels, &error);
    if (error != OPUS_OK || decoder == nullptr)
    {
        decoder = nullptr;
        return false;
    }

    decodingSamplerate = newSampleRate;
    decodingChannels = newChannels;
    opus_decoder_ctl(decoder, OPUS_RESET_STATE);
    return true;
}

AudioMetadata OpusDecoderWrapper::getMetadata(ogg_packet* packet) {
    AudioMetadata metadata;

    metadata.type = DetectedType::BUFFER_OGG_OPUS;
    
    if (packet->bytes < 8 || std::memcmp(packet->packet, "OpusTags", 8) != 0) {
        std::cerr << "Not an OpusTags packet\n";
        return metadata;
    }
    
    size_t pos = 8;
    if (pos + 4 > packet->bytes) return metadata;
    uint32_t vendor_len;
    memcpy(&vendor_len, packet->packet + pos, sizeof(vendor_len));
    pos += 4;
    
    if (pos + vendor_len > packet->bytes) return metadata;
    std::string vendor((const char*)(packet->packet + pos), vendor_len);
    pos += vendor_len;
    
    if (pos + 4 > packet->bytes) return metadata;
    uint32_t comment_count;
    memcpy(&comment_count, packet->packet + pos, sizeof(comment_count));
    pos += 4;
    
    metadata.oggMetadata.vendor = vendor;
    metadata.oggMetadata.commentsCount = comment_count;
    
    for (uint32_t i = 0; i < comment_count && pos + 4 <= packet->bytes; i++) {
        uint32_t clen;
        memcpy(&clen, packet->packet + pos, sizeof(clen));
        pos += 4;
        if (pos + clen > packet->bytes) return metadata;
        std::string comment((const char*)(packet->packet + pos), clen);
        pos += clen;
        metadata.oggMetadata.comments[comment.substr(0, comment.find('='))] = comment.substr(comment.find('=') + 1);
    }
    
    // Fill the OpusInfo struct already stored in opusInfo
    metadata.oggMetadata.opusInfo.version = opusInfo.version;
    metadata.oggMetadata.opusInfo.channels = opusInfo.channels;
    metadata.oggMetadata.opusInfo.pre_skip = opusInfo.pre_skip;
    metadata.oggMetadata.opusInfo.input_sample_rate = opusInfo.input_sample_rate;
    metadata.oggMetadata.opusInfo.output_gain = opusInfo.output_gain;
    
    if (onTrackChange) {
        onTrackChange(metadata);
    }

    return metadata;
}

bool OpusDecoderWrapper::initializeDecoder(int engineSamplerateIn, int engineChannelsIn)
{
    engineSamplerate = engineSamplerateIn;
    engineChannels = engineChannelsIn;

    const int initialChannels = std::max(1, std::min(engineChannelsIn > 0 ? engineChannelsIn : 2, 2));
    const int initialSampleRate = 48000;

    if (!ensureDecoder(initialSampleRate, initialChannels))
    {
        return false;
    }

    streamInitialized = false;
    headerParsed = false;
    packetCount = 0;
    skipSamplesPending = 0;
    totalOutputSamples = 0;
    totalSamplesExpected = -1;

    ogg_sync_init(&oy);
    return true;
}

std::pair<std::vector<float>, DecoderError> OpusDecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels)
{
    std::vector<float> decodedData;
    bool eos_seen = false;

    if (buffer.empty())
    {
        return {decodedData, DecoderError::NoError};
    }

    // Write data into ogg sync buffer
    char *oggBuffer = ogg_sync_buffer(&oy, buffer.size());
    memcpy(oggBuffer, buffer.data(), buffer.size());
    ogg_sync_wrote(&oy, buffer.size());
    buffer.clear();

    // Read and process pages
    while (ogg_sync_pageout(&oy, &og) == 1)
    {
        if (ogg_page_eos(&og))
        {
            const ogg_int64_t granule = ogg_page_granulepos(&og);
            if (granule >= 0)
            {
                if (headerParsed)
                {
                    int64_t trimmed = granule - opusInfo.pre_skip;
                    if (trimmed < 0)
                        trimmed = 0;
                    totalSamplesExpected = (trimmed * decodingSamplerate + 47999) / 48000;
                }
                else
                {
                    totalSamplesExpected = (granule * decodingSamplerate + 47999) / 48000;
                }
            }
            eos_seen = true;
        }

        if (!streamInitialized)
        {
            if (ogg_stream_init(&os, ogg_page_serialno(&og)))
            {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        // Check if this is a new stream (different serial number)
        if (streamInitialized && ogg_page_serialno(&og) != os.serialno)
        {
            ogg_stream_clear(&os);
            streamInitialized = false;
            headerParsed = false;
            packetCount = 0;
            skipSamplesPending = 0;
            totalOutputSamples = 0;
            totalSamplesExpected = -1;
            if (decoder)
            {
                opus_decoder_ctl(decoder, OPUS_RESET_STATE);
            }
        }

        if (!streamInitialized)
        {
            if (ogg_stream_init(&os, ogg_page_serialno(&og)) != 0)
            {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        if (ogg_stream_pagein(&os, &og) < 0)
        {
            continue;  // Skip corrupted page
        }

        // Extract packets from page
        while (ogg_stream_packetout(&os, &op) == 1)
        {
            auto packetData = decodePacket(&op);
            if (!packetData.empty())
            {
                decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
            }
        }
    }

    *samplerate = decodingSamplerate;
    *channels = decodingChannels;

    if (eos_seen)
    {
        const size_t fade_samples = static_cast<size_t>(decodingSamplerate * 0.005); // 5ms fade
        if (fade_samples > 0 && decodingChannels > 0)
        {
            const size_t fade_floats = fade_samples * decodingChannels;
            if (decodedData.size() > fade_floats)
            {
                size_t start_fade = decodedData.size() - fade_floats;
                for (size_t i = 0; i < fade_floats; ++i)
                {
                    float multiplier = 1.0f - (float)(i / decodingChannels) / (float)fade_samples;
                    decodedData[start_fade + i] *= multiplier;
                }
            }
        }
    }

    return {decodedData, DecoderError::NoError};
}

OpusInfo OpusDecoderWrapper::parseOpusHead(ogg_packet* packet) {
    OpusInfo head {};
    unsigned char* data = (unsigned char*)packet->packet;
    size_t len = packet->bytes;

    // Minimum size = 19 bytes (for mapping_family=0)
    if (len < 19 || std::memcmp(data, "OpusHead", 8) != 0) {
        throw std::runtime_error("Invalid OpusHead packet");
    }

    size_t pos = 8;
    head.version = data[pos++];
    head.channels = data[pos++];

    head.pre_skip = (uint16_t)(data[pos] | (data[pos+1] << 8));
    pos += 2;

    head.input_sample_rate = (uint32_t)(data[pos] |
                                       (data[pos+1] << 8) |
                                       (data[pos+2] << 16) |
                                       (data[pos+3] << 24));
    pos += 4;

    head.output_gain = (int16_t)(data[pos] | (data[pos+1] << 8));
    pos += 2;

    head.mapping_family = data[pos++];

    if (head.mapping_family != 0) {
        // Must have at least 2 + channels more bytes
        if (pos + 2 + head.channels > len) {
            printf("Invalid OpusHead: truncated channel mapping");
            return head;
        }

        head.stream_count = data[pos++];
        head.coupled_count = data[pos++];

        head.channel_mapping.resize(head.channels);
        for (uint8_t i = 0; i < head.channels; i++) {
            head.channel_mapping[i] = data[pos++];
        }
    }

    return head;
}

std::vector<float> OpusDecoderWrapper::decodePacket(ogg_packet* packet)
{
    std::vector<float> packetPcm;

    // Skip header packets (first 2 packets in Ogg Opus stream)
    if (!headerParsed)
    {
        // Try to identify if this is an Opus header
        if (packet->bytes >= 8 && memcmp(packet->packet, "OpusHead", 8) == 0)
        {
            try
            {
                opusInfo = parseOpusHead(packet);
            }
            catch (const std::exception &)
            {
                return packetPcm;
            }

            int desiredSampleRate = opusInfo.input_sample_rate > 0
                                        ? static_cast<int>(opusInfo.input_sample_rate)
                                        : 48000;
            if (desiredSampleRate < 8000)
                desiredSampleRate = 8000;
            if (desiredSampleRate > 48000)
                desiredSampleRate = 48000;

            int desiredChannels = opusInfo.channels > 0 ? opusInfo.channels : decodingChannels;
            if (desiredChannels <= 0)
                desiredChannels = 1;
            if (desiredChannels > 2)
                desiredChannels = 2;

            if (!ensureDecoder(desiredSampleRate, desiredChannels))
            {
                return packetPcm;
            }

            skipSamplesPending = static_cast<int>(
                (static_cast<int64_t>(opusInfo.pre_skip) * decodingSamplerate + 47999) / 48000);
            totalOutputSamples = 0;
            totalSamplesExpected = -1;
        }
        else if (packet->bytes >= 8 && memcmp(packet->packet, "OpusTags", 8) == 0)
        {
            // This is the OpusTags packet
            headerParsed = true;
            getMetadata(packet);
            skipSamplesPending = static_cast<int>(
                (static_cast<int64_t>(opusInfo.pre_skip) * decodingSamplerate + 47999) / 48000);
        }
        packetCount++;
        return packetPcm;
    }

    if (decoder == nullptr)
    {
        return packetPcm;
    }

    // Opus can handle frame sizes from 2.5ms to 60ms
    // We'll use buffer size to accommodate any frame size
    const int maxFrameSize = decodingSamplerate * 60 / 1000; // 60ms frame size
    std::vector<float> outputBuffer(maxFrameSize * decodingChannels);

    // Try decoding the packet
    int samples = opus_decode_float(decoder,
                                    packet->packet,
                                    packet->bytes,
                                    outputBuffer.data(),
                                    maxFrameSize,
                                    0);

    if (samples < 0)
    {
        // const char* errorMsg = opus_strerror(samples);
        // fprintf(stderr, "Opus decode error: %s (code: %d) packet size: %zu\n", 
        //         errorMsg, samples, packet->bytes);
        
        // Additional debugging info
        // if (packet->bytes < 1)
        //     fprintf(stderr, "Warning: Empty packet received\n");
        // else
        //     fprintf(stderr, "Packet starts with: %02x %02x %02x %02x\n",
        //             packet->packet[0], packet->packet[1], 
        //             packet->packet[2], packet->packet[3]);
        
        return packetPcm; // Skip invalid packet instead of throwing
    }

    if (samples > 0)
    {
        int usableSamples = samples;
        int skippedSamples = 0;

        if (skipSamplesPending > 0)
        {
            const int toSkip = std::min(skipSamplesPending, usableSamples);
            skipSamplesPending -= toSkip;
            usableSamples -= toSkip;
            skippedSamples = toSkip;
        }

        if (usableSamples <= 0)
        {
            return packetPcm;
        }

        int64_t allowedSamples = usableSamples;
        if (totalSamplesExpected >= 0)
        {
            const int64_t remaining = totalSamplesExpected - totalOutputSamples;
            if (remaining <= 0)
            {
                return packetPcm;
            }
            if (allowedSamples > remaining)
            {
                allowedSamples = remaining;
            }
        }

        if (allowedSamples <= 0)
        {
            return packetPcm;
        }

        const size_t startIndex = static_cast<size_t>(skippedSamples) * decodingChannels;
        const size_t floatsToCopy = static_cast<size_t>(allowedSamples) * decodingChannels;

        packetPcm.insert(packetPcm.end(),
                         outputBuffer.begin() + startIndex,
                         outputBuffer.begin() + startIndex + floatsToCopy);
        totalOutputSamples += allowedSamples;
    }
    return packetPcm;
}
#endif // #if defined(NO_OPUS_OGG_LIBS)

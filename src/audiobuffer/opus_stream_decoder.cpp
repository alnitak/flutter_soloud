#if !defined(NO_OPUS_OGG_LIBS)

#include "opus_stream_decoder.h"

OpusDecoderWrapper::OpusDecoderWrapper()
    : streamInitialized(false), headerParsed(false), packetCount(0)
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

        // Check if this is a new stream (different serial number)
        if (streamInitialized && ogg_page_serialno(&og) != os.serialno) {
            // Clean up old stream state
            ogg_stream_clear(&os);
            opus_decoder_destroy(decoder);
            decoder = opus_decoder_create(decodingSamplerate, decodingChannels, nullptr);
            streamInitialized = false;
            headerParsed = false;
            packetCount = 0;

            // Initialize new stream
            if (ogg_stream_init(&os, ogg_page_serialno(&og)) == 0) {
                streamInitialized = true;
            }
        }

        if (!streamInitialized) {
            if (ogg_stream_init(&os, ogg_page_serialno(&og)) != 0) {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        if (ogg_stream_pagein(&os, &og) < 0) {
            continue;  // Skip corrupted page
        }

        // Extract packets from page
        while (ogg_stream_packetout(&os, &op) == 1)
        {
            auto packetData = decodePacket(&op);
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
            // This is the Opus header, we could parse it for more info if needed
            headerParsed = false;
            opusInfo = parseOpusHead(packet);
        }
        else if (packet->bytes >= 8 && memcmp(packet->packet, "OpusTags", 8) == 0)
        {
            // This is the OpusTags packet
            headerParsed = true;
            getMetadata(packet);
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
        packetPcm.insert(packetPcm.end(),
                         outputBuffer.begin(),
                         outputBuffer.begin() + samples * decodingChannels);
    }
    return packetPcm;
}
#endif // #if defined(NO_OPUS_OGG_LIBS)

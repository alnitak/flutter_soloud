#if !defined(NO_XIPH_LIBS)

#include "opus_stream_decoder.h"
#include "../soloud_common.h"
#include <algorithm>
#include <cmath>

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

    // Opus decoder only supports 8, 12, 16, 24, and 48 kHz.
    if (newSampleRate != 8000 && newSampleRate != 12000 &&
        newSampleRate != 16000 && newSampleRate != 24000 &&
        newSampleRate != 48000)
    {
        newSampleRate = 48000;
    }

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

void OpusDecoderWrapper::getMetadata(ogg_packet* packet) {
    AudioMetadata metadata;

    metadata.type = DetectedType::BUFFER_OGG_OPUS;

    if (packet->bytes < 8 || std::memcmp(packet->packet, "OpusTags", 8) != 0) {
        return;
    }
    
    size_t pos = 8;
    if (pos + 4 > packet->bytes) return;
    uint32_t vendor_len;
    memcpy(&vendor_len, packet->packet + pos, sizeof(vendor_len));
    pos += 4;

    if (pos + vendor_len > packet->bytes) return;
    std::string vendor((const char*)(packet->packet + pos), vendor_len);
    pos += vendor_len;

    if (pos + 4 > packet->bytes) return;
    uint32_t comment_count;
    memcpy(&comment_count, packet->packet + pos, sizeof(comment_count));
    pos += 4;

    metadata.oggMetadata.vendor = vendor;
    metadata.oggMetadata.commentsCount = comment_count;

    for (uint32_t i = 0; i < comment_count && pos + 4 <= packet->bytes; i++) {
        uint32_t clen;
        memcpy(&clen, packet->packet + pos, sizeof(clen));
        pos += 4;
        if (pos + clen > packet->bytes) return;
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

std::pair<std::vector<float>, DecoderError> OpusDecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels, size_t maxOutputSamples)
{
    std::vector<float> decodedData;
    if (maxOutputSamples > 0)
    {
        decodedData.reserve(maxOutputSamples);
    }
    bool eos_seen = false;

    // Feed any newly arrived data into the Ogg sync buffer. If [buffer] is empty,
    // we still need to process pages that were already buffered by a previous
    // call, otherwise a single-shot full-file feed cannot continue after the
    // first decode run.
    if (!buffer.empty())
    {
        char *oggBuffer = ogg_sync_buffer(&oy, buffer.size());
        memcpy(oggBuffer, buffer.data(), buffer.size());
        ogg_sync_wrote(&oy, buffer.size());
        buffer.clear();
    }

    // Read and process pages, tracking byte offsets for seeking.
    while (true)
    {
        long ret = ogg_sync_pageseek(&oy, &og);
        if (ret == 0)
            break; // No complete page buffered yet; wait for more data.
        if (ret < 0)
        {
            // Bytes were skipped while searching for the next page capture
            // pattern. This always happens after a seek to a mid-page offset;
            // account for them and keep looking instead of giving up.
            mSeekIndex.bytesConsumed += static_cast<uint64_t>(-ret);
            continue;
        }

        mSeekIndex.trackPage(ret, og);

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
            decodePacket(&op, decodedData);
            if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples)
            {
                break;
            }
        }
        if (maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples)
        {
            break;
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
            SOLOUD_DEBUG_LOG("Invalid OpusHead: truncated channel mapping\n");
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

void OpusDecoderWrapper::decodePacket(ogg_packet* packet, std::vector<float>& out)
{
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
                return;
            }

            // Opus only supports specific output sample rates.
            // input_sample_rate from OpusHead is informational and may
            // not be a valid decoder rate (e.g. 44100).
            int desiredSampleRate = engineSamplerate;
            if (desiredSampleRate != 8000 && desiredSampleRate != 12000 &&
                desiredSampleRate != 16000 && desiredSampleRate != 24000 &&
                desiredSampleRate != 48000)
            {
                desiredSampleRate = 48000;
            }

            int desiredChannels = opusInfo.channels > 0 ? opusInfo.channels : decodingChannels;
            if (desiredChannels <= 0)
                desiredChannels = 1;
            if (desiredChannels > 2)
                desiredChannels = 2;

            if (!ensureDecoder(desiredSampleRate, desiredChannels))
            {
                return;
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
        return;
    }

    if (decoder == nullptr)
    {
        return;
    }

    // Opus can handle frame sizes from 2.5ms to 60ms. The scratch buffer is
    // sized for the largest possible frame and reused across packets.
    const int maxFrameSize = decodingSamplerate * 60 / 1000; // 60ms frame size
    mOutputScratch.resize(static_cast<size_t>(maxFrameSize) * decodingChannels);

    // Try decoding the packet
    int samples = opus_decode_float(decoder,
                                    packet->packet,
                                    static_cast<opus_int32>(packet->bytes),
                                    mOutputScratch.data(),
                                    maxFrameSize,
                                    0);

    if (samples < 0)
    {
        return; // Skip invalid packet instead of throwing
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
            return;
        }

        int64_t allowedSamples = usableSamples;
        if (totalSamplesExpected >= 0)
        {
            const int64_t remaining = totalSamplesExpected - totalOutputSamples;
            if (remaining <= 0)
            {
                return;
            }
            if (allowedSamples > remaining)
            {
                allowedSamples = remaining;
            }
        }

        if (allowedSamples <= 0)
        {
            return;
        }

        const size_t startIndex = static_cast<size_t>(skippedSamples) * decodingChannels;
        const size_t floatsToCopy = static_cast<size_t>(allowedSamples) * decodingChannels;

        out.insert(out.end(),
                   mOutputScratch.begin() + startIndex,
                   mOutputScratch.begin() + startIndex + floatsToCopy);
        totalOutputSamples += allowedSamples;
    }
}

void OpusDecoderWrapper::prepareForSeek(uint64_t targetSample)
{
    // Preserve the already-parsed header and decoder configuration, but reset
    // the Ogg page-level state so that mid-stream data can be synced again.
    ogg_sync_clear(&oy);
    ogg_sync_init(&oy);
    if (streamInitialized)
    {
        const long serial = os.serialno;
        ogg_stream_clear(&os);
        ogg_stream_init(&os, serial);
    }
    mSeekIndex.clear();
    skipSamplesPending = 0;
    totalOutputSamples = static_cast<int64_t>(targetSample);
    packetCount = 2; // OpusHead and OpusTags have already been parsed.
}

bool OpusDecoderWrapper::canSeekToTime(double seconds) const
{
    if (!headerParsed || seconds <= 0.0)
        return false;
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * 48000.0)) + opusInfo.pre_skip;
    return mSeekIndex.covers(targetGranule);
}

uint64_t OpusDecoderWrapper::timeToByteOffset(double seconds)
{
    if (!headerParsed || seconds <= 0.0)
        return 0;
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * 48000.0)) + opusInfo.pre_skip;
    return mSeekIndex.byteOffsetFor(targetGranule);
}

double OpusDecoderWrapper::getDuration() const
{
    if (totalSamplesExpected <= 0 || decodingSamplerate <= 0)
        return -1.0;
    return static_cast<double>(totalSamplesExpected) /
           static_cast<double>(decodingSamplerate);
}

#endif // #if defined(NO_XIPH_LIBS)

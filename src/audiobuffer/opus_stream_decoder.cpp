#include "metadata_bounds.h"
#if !defined(NO_XIPH_LIBS)

#include "opus_stream_decoder.h"
#include "../soloud_common.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>

OpusDecoderWrapper::OpusDecoderWrapper()
    : decoder(nullptr),
      engineSamplerate(48000),
      engineChannels(2),
      decodingSamplerate(48000),
      decodingChannels(2),
      sourceChannels(2),
      syncInitialized(false),
      streamInitialized(false),
      headerParsed(false),
      packetCount(0),
      skipSamplesPending(0),
      totalOutputSamples(0),
      totalSamplesExpected(-1),
      mDataEnded(false),
      mDrained(false)
{
}

OpusDecoderWrapper::~OpusDecoderWrapper()
{
    if (decoder)
    {
        opus_multistream_decoder_destroy(decoder);
        decoder = nullptr;
    }

    if (streamInitialized)
    {
        ogg_stream_clear(&os);
    }
    if (syncInitialized)
    {
        ogg_sync_clear(&oy);
    }
}

namespace {

// Stereo downmix of mapping family 1 (Vorbis channel order), RFC 7845
// section 5.1.1.5 (the same matrices as opusfile), for 3 to 8 channels.
const float kStereoDownmix[6][8][2] = {
    // 3.0: L, C, R
    {{0.5858f, 0.0f}, {0.4142f, 0.4142f}, {0.0f, 0.5858f}},
    // quadraphonic: FL, FR, RL, RR
    {{0.4226f, 0.0f}, {0.0f, 0.4226f}, {0.366f, 0.2114f}, {0.2114f, 0.366f}},
    // 5.0: FL, C, FR, RL, RR
    {{0.651f, 0.0f}, {0.46f, 0.46f}, {0.0f, 0.651f}, {0.5636f, 0.3254f},
     {0.3254f, 0.5636f}},
    // 5.1: FL, C, FR, RL, RR, LFE
    {{0.529f, 0.0f}, {0.3741f, 0.3741f}, {0.0f, 0.529f}, {0.4582f, 0.2645f},
     {0.2645f, 0.4582f}, {0.3741f, 0.3741f}},
    // 6.1: FL, C, FR, SL, SR, RC, LFE
    {{0.4553f, 0.0f}, {0.322f, 0.322f}, {0.0f, 0.4553f}, {0.3943f, 0.2277f},
     {0.2277f, 0.3943f}, {0.2788f, 0.2788f}, {0.322f, 0.322f}},
    // 7.1: FL, C, FR, SL, SR, RL, RR, LFE
    {{0.3886f, 0.0f}, {0.2748f, 0.2748f}, {0.0f, 0.3886f}, {0.3366f, 0.1943f},
     {0.1943f, 0.3366f}, {0.3366f, 0.1943f}, {0.1943f, 0.3366f},
     {0.2748f, 0.2748f}},
};

bool isOpusHead(const ogg_packet &p)
{
    return p.bytes >= 8 && std::memcmp(p.packet, "OpusHead", 8) == 0;
}

// Granule (48 kHz) of the first sample of the first packet that starts on
// [page]: the page's granule minus the durations of the packets that end on
// it. A packet continued from the previous page is not decoded after a seek
// (libogg drops it), so it does not count.
ogg_int64_t pageStartGranule(const ogg_page &page)
{
    const ogg_int64_t granule = ogg_page_granulepos(&page);
    const unsigned char *header = page.header;
    const int segments = header[26];
    bool skipping = ogg_page_continued(&page) != 0;
    ogg_int64_t total = 0;
    long pos = 0;
    long len = 0;
    for (int i = 0; i < segments && 27 + i < page.header_len; ++i)
    {
        const int v = header[27 + i];
        len += v;
        if (v < 255)
        {
            if (!skipping && len > 0 && pos + len <= page.body_len)
            {
                const int n = opus_packet_get_nb_samples(page.body + pos,
                                                         static_cast<opus_int32>(len),
                                                         48000);
                if (n > 0)
                    total += n;
            }
            skipping = false;
            pos += len;
            len = 0;
        }
    }
    return granule - total;
}

} // namespace

bool OpusDecoderWrapper::ensureDecoder(int newSampleRate, const OpusInfo &head)
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

    // The stream layout: family 0 is one stream of 1 or 2 channels; the
    // other families give the streams and the channel mapping (RFC 7845).
    int channels = head.channels;
    int streams = 1;
    int coupled = 0;
    std::vector<unsigned char> mapping;
    if (head.mapping_family == 0 || head.channel_mapping.empty())
    {
        if (channels <= 0)
            channels = 1;
        if (channels > 2)
            channels = 2;
        coupled = channels - 1;
        mapping = {0, 1};
        mapping.resize(channels);
    }
    else
    {
        streams = head.stream_count;
        coupled = head.coupled_count;
        mapping.assign(head.channel_mapping.begin(), head.channel_mapping.end());
    }

    if (decoder && newSampleRate == decodingSamplerate &&
        channels == sourceChannels && streams == decoderStreams &&
        coupled == decoderCoupled && mapping == decoderMapping)
    {
        downmixFamily1 = head.mapping_family == 1;
        opus_multistream_decoder_ctl(decoder, OPUS_RESET_STATE);
        return true;
    }

    if (decoder)
    {
        opus_multistream_decoder_destroy(decoder);
        decoder = nullptr;
    }

    int error = OPUS_OK;
    decoder = opus_multistream_decoder_create(newSampleRate, channels, streams,
                                              coupled, mapping.data(), &error);
    if (error != OPUS_OK || decoder == nullptr)
    {
        decoder = nullptr;
        return false;
    }

    decoderStreams = streams;
    decoderCoupled = coupled;
    decoderMapping = mapping;
    downmixFamily1 = head.mapping_family == 1;
    decodingSamplerate = newSampleRate;
    sourceChannels = channels;
    decodingChannels = std::min(channels, 2);
    opus_multistream_decoder_ctl(decoder, OPUS_RESET_STATE);
    return true;
}

void OpusDecoderWrapper::getMetadata(ogg_packet* packet) {
    AudioMetadata metadata;

    metadata.type = DetectedType::BUFFER_OGG_OPUS;

    if (packet->bytes < 8 || std::memcmp(packet->packet, "OpusTags", 8) != 0) {
        return;
    }
    
    size_t pos = 8;
    if (!metadataHasBytes(static_cast<size_t>(packet->bytes), pos, 4)) return;
    uint32_t vendor_len;
    memcpy(&vendor_len, packet->packet + pos, sizeof(vendor_len));
    pos += 4;

    if (!metadataHasBytes(static_cast<size_t>(packet->bytes), pos, vendor_len)) return;
    std::string vendor((const char*)(packet->packet + pos), vendor_len);
    pos += vendor_len;

    if (!metadataHasBytes(static_cast<size_t>(packet->bytes), pos, 4)) return;
    uint32_t comment_count;
    memcpy(&comment_count, packet->packet + pos, sizeof(comment_count));
    pos += 4;

    metadata.oggMetadata.vendor = vendor;
    metadata.oggMetadata.commentsCount = comment_count;

    for (uint32_t i = 0; i < comment_count && metadataHasBytes(static_cast<size_t>(packet->bytes), pos, 4); i++) {
        uint32_t clen;
        memcpy(&clen, packet->packet + pos, sizeof(clen));
        pos += 4;
        if (!metadataHasBytes(static_cast<size_t>(packet->bytes), pos, clen)) return;
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

    OpusInfo initialHead {};
    initialHead.channels = static_cast<uint8_t>(
        std::max(1, std::min(engineChannelsIn > 0 ? engineChannelsIn : 2, 2)));
    const int initialSampleRate = 48000;

    if (!ensureDecoder(initialSampleRate, initialHead))
    {
        return false;
    }

    if (streamInitialized)
    {
        ogg_stream_clear(&os);
        streamInitialized = false;
    }
    if (syncInitialized)
    {
        ogg_sync_clear(&oy);
        syncInitialized = false;
    }

    headerParsed = false;
    packetCount = 0;
    skipSamplesPending = 0;
    totalOutputSamples = 0;
    totalSamplesExpected = -1;
    mDataEnded = false;
    mDrained = false;
    mLinkStart = 0;
    mLinkPreSkip = 0;
    mLinkLastGranule = 0;
    mSeekResync = false;
    mSeekIndex.clear();

    ogg_sync_init(&oy);
    syncInitialized = true;
    return true;
}

ogg_int64_t OpusDecoderWrapper::timelineGranule(ogg_int64_t granule) const
{
    if (granule < 0)
        return -1;
    return mLinkStart + std::max<ogg_int64_t>(0, granule - mLinkPreSkip);
}

int OpusDecoderWrapper::decodeNextPacket(std::vector<float> &out)
{
    ogg_packet p;
    const int r = ogg_stream_packetpeek(&os, &p);
    if (r == 0)
        return 0;
    if (r < 0)
        return 1; // A hole (lost or skipped data); peeking consumed it.
    // A new link starts: one call returns one link, with its channel count.
    if (!out.empty() && isOpusHead(p))
        return -1;
    ogg_stream_packetout(&os, &p);
    decodePacket(&p, out);
    return 1;
}

std::pair<std::vector<float>, DecoderError> OpusDecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels, size_t maxOutputSamples)
{
    std::vector<float> decodedData;
    if (maxOutputSamples > 0)
    {
        decodedData.reserve(maxOutputSamples);
    }
    const auto capped = [&]() {
        return maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples;
    };

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

    bool newLink = false;

    // First, drain any packets left in [os] from a previous bounded decode
    // pass, or held back because they start a new link.
    if (streamInitialized)
    {
        int r;
        while (!capped() && (r = decodeNextPacket(decodedData)) != 0)
        {
            if (r < 0)
            {
                newLink = true;
                break;
            }
        }
    }

    // Read and process pages, tracking byte offsets for seeking.
    while (!capped() && !newLink)
    {
        long ret = ogg_sync_pageseek(&oy, &og);
        if (ret == 0)
        {
            if (mDataEnded)
            {
                mDrained = true;
            }
            break; // No complete page buffered yet; wait for more data.
        }
        if (ret < 0)
        {
            // Bytes were skipped while searching for the next page capture
            // pattern. This always happens after a seek to a mid-page offset;
            // account for them and keep looking instead of giving up.
            mSeekIndex.bytesConsumed += static_cast<uint64_t>(-ret);
            continue;
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
                opus_multistream_decoder_ctl(decoder, OPUS_RESET_STATE);
            }
            // The next link starts where this one ends on the timeline.
            mLinkStart += std::max<int64_t>(0, mLinkLastGranule - mLinkPreSkip);
            mLinkLastGranule = 0;
            mLinkPreSkip = 0;
            mSeekResync = false;
        }

        const ogg_int64_t granule = ogg_page_granulepos(&og);
        mSeekIndex.trackPage(ret, og, timelineGranule(granule));
        if (granule > mLinkLastGranule)
            mLinkLastGranule = granule;

        if (ogg_page_eos(&og))
        {
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
        }

        if (!streamInitialized)
        {
            if (ogg_stream_init(&os, ogg_page_serialno(&og)) != 0)
            {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        if (mSeekResync && headerParsed && granule >= 0)
        {
            resyncAfterSeek(og);
        }

        if (ogg_stream_pagein(&os, &og) < 0)
        {
            continue;  // Skip corrupted page
        }

        // Extract packets from page
        int r;
        while (!capped() && (r = decodeNextPacket(decodedData)) != 0)
        {
            if (r < 0)
            {
                newLink = true;
                break;
            }
        }
    }

    *samplerate = decodingSamplerate;
    *channels = decodingChannels;

    return {std::move(decodedData), DecoderError::NoError};
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
        if (!metadataHasBytes(len, pos, static_cast<uint64_t>(head.channels) + 2)) {
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

            if (!ensureDecoder(desiredSampleRate, opusInfo))
            {
                return;
            }
            mLinkPreSkip = opusInfo.pre_skip;

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
    mOutputScratch.resize(static_cast<size_t>(maxFrameSize) * sourceChannels);

    // Try decoding the packet
    int samples = opus_multistream_decode_float(decoder,
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

        // A 5 ms fade over the last frames of the stream, once its end is
        // known, wherever the decode() calls are cut.
        const int64_t fadeFrames = decodingSamplerate / 200;
        const int64_t fadeStart = totalSamplesExpected >= 0
            ? totalSamplesExpected - fadeFrames
            : INT64_MAX;

        const int inCh = sourceChannels;
        const int outCh = decodingChannels;
        const float(*matrix)[2] =
            downmixFamily1 && inCh >= 3 && inCh <= 8 ? kStereoDownmix[inCh - 3] : nullptr;
        const size_t base = out.size();
        out.resize(base + static_cast<size_t>(allowedSamples) * outCh);
        float *dst = out.data() + base;
        for (int64_t i = 0; i < allowedSamples; ++i)
        {
            const float *src =
                mOutputScratch.data() + static_cast<size_t>(skippedSamples + i) * inCh;
            float l = 0.0f;
            float r = 0.0f;
            if (matrix)
            {
                for (int c = 0; c < inCh; ++c)
                {
                    l += src[c] * matrix[c][0];
                    r += src[c] * matrix[c][1];
                }
            }
            else
            {
                // One or two channels, or a layout without a known downmix:
                // the first channels.
                l = src[0];
                if (outCh > 1)
                    r = src[1];
            }
            const int64_t pos = totalOutputSamples + i;
            if (pos >= fadeStart && fadeFrames > 0)
            {
                const float m = 1.0f - (float)(pos - fadeStart) / (float)fadeFrames;
                l *= m;
                r *= m;
            }
            dst[i * outCh] = l;
            if (outCh > 1)
                dst[i * outCh + 1] = r;
        }
        totalOutputSamples += allowedSamples;
    }
}

void OpusDecoderWrapper::prepareForSeek(uint64_t targetSample, uint64_t byteOffset)
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
    // The data fed next starts at [byteOffset] in the file, so the pages
    // indexed from here on are counted from there.
    mSeekIndex.clear(byteOffset);
    if (decoder)
    {
        opus_multistream_decoder_ctl(decoder, OPUS_RESET_STATE);
    }
    // [targetSample] counts interleaved output samples from the start of the
    // file (PullBufferStream passes seconds * rate * channels).
    mSeekTarget = static_cast<int64_t>(
        targetSample / static_cast<uint64_t>(std::max(1, decodingChannels)));
    mSeekResync = true;
    skipSamplesPending = 0;
    totalOutputSamples =
        std::max<int64_t>(0, mSeekTarget - mLinkStart * decodingSamplerate / 48000);
    packetCount = 2; // OpusHead and OpusTags have already been parsed.
    mDataEnded = false;
    mDrained = false;
}

void OpusDecoderWrapper::resyncAfterSeek(const ogg_page &page)
{
    mSeekResync = false;
    // Decoding resumes at the first packet that starts on [page]: drop the
    // samples up to the target, and count from where decoding resumes.
    const int64_t rate = decodingSamplerate;
    const int64_t start48 = pageStartGranule(page) - opusInfo.pre_skip;
    const int64_t startOut = start48 >= 0 ? start48 * rate / 48000
                                          : -((-start48 * rate + 47999) / 48000);
    const int64_t targetOut =
        std::max<int64_t>(0, mSeekTarget - mLinkStart * rate / 48000);
    if (targetOut > startOut)
    {
        skipSamplesPending = static_cast<int>(
            std::min<int64_t>(targetOut - startOut, INT32_MAX));
        totalOutputSamples = targetOut;
    }
    else
    {
        skipSamplesPending = 0;
        totalOutputSamples = startOut;
    }
}

bool OpusDecoderWrapper::hasPendingData() const
{
    if (mDataEnded && mDrained)
    {
        return false;
    }
    if (streamInitialized)
    {
        ogg_packet dummyOp;
        if (ogg_stream_packetpeek(const_cast<ogg_stream_state*>(&os), &dummyOp) == 1)
        {
            return true;
        }
    }
    if (oy.fill > oy.returned)
    {
        return true;
    }
    if (mDataEnded && !mDrained)
    {
        return true;
    }
    return false;
}

bool OpusDecoderWrapper::canSeekToTime(double seconds) const
{
    if (!headerParsed || seconds <= 0.0)
        return false;
    // The index holds times from the start of the file (48 kHz).
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * 48000.0));
    return mSeekIndex.covers(targetGranule);
}

uint64_t OpusDecoderWrapper::timeToByteOffset(double seconds)
{
    if (!headerParsed || seconds <= 0.0)
        return 0;
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * 48000.0));
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

#if !defined(NO_XIPH_LIBS)

#include "vorbis_stream_decoder.h"
#include "../soloud_common.h"
#include "../simd/simd_pcm.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <utility>

VorbisDecoderWrapper::VorbisDecoderWrapper()
    : vorbisInitialized(false), headersInitialized(false), syncInitialized(false),
      streamInitialized(false), headerParsed(false), packetCount(0),
      mDataEnded(false), mDrained(false)
{
}

VorbisDecoderWrapper::~VorbisDecoderWrapper() {
    if (vorbisInitialized) {
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
    }
    if (headersInitialized) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
    }

    if (streamInitialized) {
        ogg_stream_clear(&os);
    }
    if (syncInitialized) {
        ogg_sync_clear(&oy);
    }
}

bool VorbisDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    if (vorbisInitialized) {
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbisInitialized = false;
    }
    if (headersInitialized) {
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);
        headersInitialized = false;
    }
    if (streamInitialized) {
        ogg_stream_clear(&os);
        streamInitialized = false;
    }
    if (syncInitialized) {
        ogg_sync_clear(&oy);
        syncInitialized = false;
    }

    decodingChannels = engineChannels > 2 ? 2 : (engineChannels<=0 ? 1 : engineChannels);
    // Choose the best sample rate nearest to the engine samplerate
    if (engineSamplerate <= 8000) decodingSamplerate = 8000;
    else if (engineSamplerate <= 12000) decodingSamplerate = 12000;
    else if (engineSamplerate <= 16000) decodingSamplerate = 16000;
    else if (engineSamplerate <= 24000) decodingSamplerate = 24000;
    else if (engineSamplerate <= 48000) decodingSamplerate = 48000;
    else decodingSamplerate = 96000;

    headerParsed = false;
    packetCount = 0;
    mDataEnded = false;
    mDrained = false;
    mLinkStart = 0;
    mLinkLastGranule = 0;
    mLinkFrames = 0;
    mLinkEnd = -1;
    mTotalSamples = -1;
    mSeekIndex.clear();

    vorbis_info_init(&vi);
    vorbis_comment_init(&vc);
    headersInitialized = true;
    ogg_sync_init(&oy);
    syncInitialized = true;
    return true;
}

AudioMetadata VorbisDecoderWrapper::getMetadata() {
    AudioMetadata metadata;

    metadata.type = DetectedType::BUFFER_OGG_VORBIS;

    metadata.oggMetadata.commentsCount = vc.comments;
    if (vc.vendor != nullptr)
        metadata.oggMetadata.vendor = vc.vendor;

    metadata.oggMetadata.vorbisInfo.version = vi.version;
    metadata.oggMetadata.vorbisInfo.rate = vi.rate;
    metadata.oggMetadata.vorbisInfo.channels = vi.channels;
    metadata.oggMetadata.vorbisInfo.bitrate_upper = vi.bitrate_upper;
    metadata.oggMetadata.vorbisInfo.bitrate_nominal = vi.bitrate_nominal;
    metadata.oggMetadata.vorbisInfo.bitrate_lower = vi.bitrate_lower;
    metadata.oggMetadata.vorbisInfo.bitrate_window = vi.bitrate_window;

    for (uint32_t i = 0; i < vc.comments; i++) {
        char* comment = vc.user_comments[i];
        int length = vc.comment_lengths[i];
        std::string commentStr(comment, length);
        size_t separator = commentStr.find('=');
        if (separator != std::string::npos) {
            std::string key = commentStr.substr(0, separator);
            std::string value = commentStr.substr(separator + 1);
            
            metadata.oggMetadata.comments[key] = value;
        }
    }

    if (onTrackChange) {
        onTrackChange(metadata);
    }

    return metadata;
}

std::pair<std::vector<float>, DecoderError> VorbisDecoderWrapper::decode(std::vector<unsigned char>& buffer,
                                               int* samplerate,
                                               int* channels, size_t maxOutputSamples) {
    std::vector<float> decodedData;
    if (maxOutputSamples > 0) {
        decodedData.reserve(maxOutputSamples);
    }
    const auto capped = [&]() {
        return maxOutputSamples > 0 && decodedData.size() >= maxOutputSamples;
    };
    // The format of the samples returned: that of the link they come from.
    const auto reportFormat = [&]() {
        if (headerParsed && samplerate && channels) {
            *samplerate = static_cast<int>(vi.rate);
            *channels   = static_cast<int>(vi.channels);
        }
    };

    // Feed any newly arrived data into the Ogg sync buffer. If [buffer] is empty,
    // we still need to process pages that were already buffered by a previous
    // call, otherwise a single-shot full-file feed cannot continue after the
    // first decode run.
    if (!buffer.empty()) {
        char* oggBuffer = ogg_sync_buffer(&oy, buffer.size());
        memcpy(oggBuffer, buffer.data(), buffer.size());
        ogg_sync_wrote(&oy, buffer.size());
        buffer.clear();
    }

    // First, drain any packets left in [os] from a previous bounded decode pass
    if (streamInitialized) {
        while (!capped() && ogg_stream_packetout(&os, &op) == 1) {
            decodePacket(&op, decodedData);
        }
    }

    // Process available pages, tracking byte offsets for seeking.
    while (!capped()) {
        long ret = ogg_sync_pageseek(&oy, &og);
        if (ret == 0) {
            if (mDataEnded) {
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

        // Check for a new stream (different serial number) or a BOS page
        const bool isNewStream = streamInitialized && ogg_page_serialno(&og) != os.serialno;
        bool isBOS = ogg_page_bos(&og);

        if ((isNewStream || isBOS) && streamInitialized) {
            // The next link starts where this one ends on the timeline.
            mLinkStart += mLinkLastGranule;
            mLinkLastGranule = 0;
        }

        const ogg_int64_t granule = ogg_page_granulepos(&og);
        mSeekIndex.trackPage(ret, og, granule >= 0 ? mLinkStart + granule : -1);
        if (granule > mLinkLastGranule) {
            mLinkLastGranule = granule;
        }

        // The samples decoded so far belong to the previous link: return
        // them with its format before this page resets it.
        bool returnAfterPage = false;
        if ((isNewStream || isBOS) && !decodedData.empty()) {
            reportFormat();
            returnAfterPage = true;
        }

        if (!streamInitialized) {
            if (ogg_stream_init(&os, ogg_page_serialno(&og))) {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        // Reset decoder state for new streams or when we see a BOS page
        if (isNewStream || isBOS) {
            // Clean up existing state if any
            if (streamInitialized) {
                ogg_stream_clear(&os);
                if (vorbisInitialized) {
                    vorbis_block_clear(&vb);
                    vorbis_dsp_clear(&vd);
                    vorbisInitialized = false;
                }
                vorbis_info_clear(&vi);
                vorbis_comment_clear(&vc);
            }

            // Initialize for new stream
            vorbis_info_init(&vi);
            vorbis_comment_init(&vc);

            if (ogg_stream_init(&os, ogg_page_serialno(&og))) {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }

            streamInitialized = true;
            headerParsed = false;
            packetCount = 0;
            mLinkFrames = 0;
            mLinkEnd = -1;
        }

        if (ogg_page_eos(&og) && granule >= 0) {
            mLinkEnd = granule;
            mTotalSamples = mLinkStart + granule;
        }

        // Process the page
        if (ogg_stream_pagein(&os, &og) < 0) {
            // Skip corrupted pages
            continue;
        }

        // If this is a BOS page, ensure we're ready for header processing
        if (isBOS) {
            headerParsed = false;
            packetCount = 0;
        }

        if (returnAfterPage) {
            // This page's packets are decoded by the next call.
            return {std::move(decodedData), DecoderError::NoError};
        }

        // Extract packets from page
        while (!capped() && ogg_stream_packetout(&os, &op) == 1) {
            decodePacket(&op, decodedData);
        }
    }

    reportFormat();

    return {std::move(decodedData), DecoderError::NoError};
}


void VorbisDecoderWrapper::decodePacket(ogg_packet* packet, std::vector<float>& out) {
    // Handle header packets
    if (!headerParsed) {
        // First packet must be BOS for proper Vorbis stream
        if (packetCount == 0) {
            SOLOUD_DEBUG_LOG("First packet info - size: %ld, b_o_s: %ld, packetno: %lld, bytes[0]: %02x\n",
                packet->bytes, packet->b_o_s, packet->packetno,
                packet->bytes > 0 ? (unsigned char)packet->packet[0] : 0);

            // Check if this looks like a Vorbis header packet (should start with 0x01)
            if (packet->bytes > 0 && packet->packet[0] != 0x01) {
                SOLOUD_DEBUG_LOG("First packet is not a Vorbis identification header\n");
                return;
            }
        }

        // Only process headers if we're within the first 3 packets
        if (packetCount < 3) {
            // Verify packet numbers are sequential and small
            if (packet->packetno != packetCount) {
                SOLOUD_DEBUG_LOG("Unexpected packet number %lld for header %d\n",
                    packet->packetno, packetCount);
                return;
            }

            int ret = vorbis_synthesis_headerin(&vi, &vc, packet);
            if (ret != 0) {
                SOLOUD_DEBUG_LOG("Error processing Vorbis header %d: code %d\n", packetCount, ret);
                SOLOUD_DEBUG_LOG("Packet size: %ld, B_O_S: %ld, packetno: %lld\n",
                    packet->bytes, packet->b_o_s, packet->packetno);
                // Reset state on header failure
                headerParsed = false;
                packetCount = 0;
                return;
            }

            packetCount++;

            // Initialize decoder after all headers are processed
            if (packetCount == 3) {
                ret = vorbis_synthesis_init(&vd, &vi);
                if (ret == 0) {
                    ret = vorbis_block_init(&vd, &vb);
                    if (ret == 0) {
                        vorbisInitialized = true;
                        headerParsed = true;
                        getMetadata();
                    } else {
                        SOLOUD_DEBUG_LOG("Failed to initialize vorbis block: code %d\n", ret);
                        vorbis_dsp_clear(&vd);
                        headerParsed = false;
                        packetCount = 0;
                    }
                } else {
                    SOLOUD_DEBUG_LOG("Failed to initialize vorbis synthesis: code %d\n", ret);
                    headerParsed = false;
                    packetCount = 0;
                }
            }
            return; // no PCM during header parsing
        }
    }

    if (!vorbisInitialized) {
        return;
    }

    // Decode an audio packet
    if (vorbis_synthesis(&vb, packet) == 0) {
        vorbis_synthesis_blockin(&vd, &vb);
    }

    float** pcm;
    int samples;
    while ((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
        const int chs = vi.channels;
        // After a seek the position in the link is not known until libvorbis
        // knows the granule of the samples it returns.
        if (mLinkFrames < 0 && vd.granulepos >= 0) {
            mLinkFrames = std::max<ogg_int64_t>(0, vd.granulepos - samples);
        }
        // libvorbis trims the last packet to the EOS granule; trim here too
        // in case it could not (no granule known before the end).
        int keep = samples;
        if (mLinkEnd >= 0 && mLinkFrames >= 0 && mLinkFrames + keep > mLinkEnd) {
            keep = static_cast<int>(std::max<ogg_int64_t>(0, mLinkEnd - mLinkFrames));
        }
        if (chs > 0 && keep > 0) {
            const size_t oldSize = out.size();
            const size_t addFloats = static_cast<size_t>(keep) * static_cast<size_t>(chs);
            out.resize(oldSize + addFloats);
            float *dst = out.data() + oldSize;
            if (chs == 1) {
                std::memcpy(dst, pcm[0], static_cast<size_t>(keep) * sizeof(float));
            } else if (chs == 2) {
                SoLoud::interleaveStereoFloats(pcm[0], pcm[1], dst, static_cast<unsigned int>(keep));
            } else {
                for (int i = 0; i < keep; i++) {
                    float *frameDst = dst + static_cast<size_t>(i) * static_cast<size_t>(chs);
                    for (int ch = 0; ch < chs; ch++) {
                        frameDst[ch] = pcm[ch][i];
                    }
                }
            }
            applyFades(dst, keep, chs);
        }
        vorbis_synthesis_read(&vd, samples);
        if (mLinkFrames >= 0) {
            mLinkFrames += keep;
        }
    }
}

void VorbisDecoderWrapper::applyFades(float *dst, int frames, int chs)
{
    // A 2 ms fade-in at the start of each link and a 5 ms fade-out at its
    // end (once the EOS granule is known), by position in the link, so the
    // fades do not depend on how decode() calls are cut.
    if (mLinkFrames < 0 || vi.rate <= 0) {
        return;
    }
    const ogg_int64_t fadeIn = static_cast<ogg_int64_t>(vi.rate * 0.002);
    const ogg_int64_t fadeOut = static_cast<ogg_int64_t>(vi.rate * 0.005);
    const ogg_int64_t outStart = mLinkEnd >= 0 ? mLinkEnd - fadeOut : -1;
    const ogg_int64_t first = mLinkFrames;
    const ogg_int64_t last = mLinkFrames + frames;
    if (first < fadeIn || (outStart >= 0 && last > outStart)) {
        for (int i = 0; i < frames; ++i) {
            const ogg_int64_t pos = first + i;
            float m = 1.0f;
            if (pos < fadeIn) {
                m = static_cast<float>(pos) / static_cast<float>(fadeIn);
            }
            if (outStart >= 0 && pos >= outStart && fadeOut > 0) {
                m *= 1.0f - static_cast<float>(pos - outStart) / static_cast<float>(fadeOut);
            }
            if (m != 1.0f) {
                float *frame = dst + static_cast<size_t>(i) * static_cast<size_t>(chs);
                for (int c = 0; c < chs; ++c) {
                    frame[c] *= m;
                }
            }
        }
    }
}

void VorbisDecoderWrapper::prepareForSeek(uint64_t targetSample, uint64_t byteOffset)
{
    (void)targetSample;
    // Preserve the already-parsed headers and decoder state, but reset the
    // Ogg page-level state so that mid-stream data can be synced again.
    if (vorbisInitialized) {
        vorbis_synthesis_restart(&vd);
    }
    // The position in the link is known again once libvorbis knows the
    // granule of the samples it returns.
    mLinkFrames = -1;
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
    mDataEnded = false;
    mDrained = false;
}

bool VorbisDecoderWrapper::hasPendingData() const {
    if (mDataEnded && mDrained) {
        return false;
    }
    if (streamInitialized) {
        ogg_packet dummyOp;
        if (ogg_stream_packetpeek(const_cast<ogg_stream_state*>(&os), &dummyOp) == 1) {
            return true;
        }
    }
    if (oy.fill > oy.returned) {
        return true;
    }
    if (mDataEnded && !mDrained) {
        return true;
    }
    return false;
}

bool VorbisDecoderWrapper::canSeekToTime(double seconds) const
{
    if (!headerParsed || seconds <= 0.0)
        return false;
    // The index holds times from the start of the file (chained links).
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * static_cast<double>(vi.rate)));
    return mSeekIndex.covers(targetGranule);
}

uint64_t VorbisDecoderWrapper::timeToByteOffset(double seconds)
{
    if (!headerParsed || seconds <= 0.0)
        return 0;
    const ogg_int64_t targetGranule = static_cast<ogg_int64_t>(
        std::floor(seconds * static_cast<double>(vi.rate)));
    return mSeekIndex.byteOffsetFor(targetGranule);
}

double VorbisDecoderWrapper::getDuration() const
{
    if (mTotalSamples <= 0 || vi.rate <= 0)
        return -1.0;
    return static_cast<double>(mTotalSamples) / static_cast<double>(vi.rate);
}

#endif // #if defined(NO_XIPH_LIBS)

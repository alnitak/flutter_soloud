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

AudioMetadata VorbisDecoderWrapper::getMetadata() {
    AudioMetadata metadata;

    metadata.type = DetectedType::BUFFER_OGG_VORBIS;

    metadata.oggMetadata.commentsCount = vc.comments;

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
                                               int* channels) {
    std::vector<float> decodedData;
    ogg_int64_t total_samples = -1;
    bool eos_seen = false;

    if (buffer.empty()) {
        return {decodedData, DecoderError::NoError};
    }

    // Write new bytes into ogg buffer
    char* oggBuffer = ogg_sync_buffer(&oy, buffer.size());
    memcpy(oggBuffer, buffer.data(), buffer.size());
    ogg_sync_wrote(&oy, buffer.size());
    buffer.clear();

    // Process available pages
    while (ogg_sync_pageout(&oy, &og) == 1) {
        if (ogg_page_eos(&og)) {
            total_samples = ogg_page_granulepos(&og);
            eos_seen = true;
        }
        
        if (!streamInitialized) {
            if (ogg_stream_init(&os, ogg_page_serialno(&og))) {
                return {decodedData, DecoderError::FailedToCreateDecoder};
            }
            streamInitialized = true;
        }

        // Check for beginning of stream or new stream
        bool isNewStream = !streamInitialized || ogg_page_serialno(&og) != os.serialno;
        bool isBOS = ogg_page_bos(&og);

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

        // Extract packets from page
        while (ogg_stream_packetout(&os, &op) == 1) {
            auto packetData = decodePacket(&op);
            decodedData.insert(decodedData.end(), packetData.begin(), packetData.end());
        }
    }

    if (total_samples != -1 && vi.channels > 0) {
        size_t total_floats = total_samples * vi.channels;
        if (decodedData.size() > total_floats) {
            decodedData.resize(total_floats);
        }
    }

    if (eos_seen) {
        const size_t fade_samples = (size_t)(vi.rate * 0.005); // 5ms fade
        if (fade_samples > 0 && vi.channels > 0) {
            const size_t fade_floats = fade_samples * vi.channels;
            if (decodedData.size() > fade_floats) {
                size_t start_fade = decodedData.size() - fade_floats;
                for (size_t i = 0; i < fade_floats; ++i) {
                    float multiplier = 1.0f - (float)(i / vi.channels) / (float)fade_samples;
                    decodedData[start_fade + i] *= multiplier;
                }
            }
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

    // Handle header packets
    if (!headerParsed) {
        // First packet must be BOS for proper Vorbis stream
        if (packetCount == 0) {
            // Debug output for first packet
            fprintf(stderr, "First packet info - size: %ld, b_o_s: %ld, packetno: %lld, bytes[0]: %02x\n",
                packet->bytes, packet->b_o_s, packet->packetno,
                packet->bytes > 0 ? (unsigned char)packet->packet[0] : 0);
                
            // Check if this looks like a Vorbis header packet (should start with 0x01)
            if (packet->bytes > 0 && packet->packet[0] != 0x01) {
                fprintf(stderr, "First packet is not a Vorbis identification header\n");
                return packetPcm;
            }
        }

        // Only process headers if we're within the first 3 packets
        if (packetCount < 3) {
            // Verify packet numbers are sequential and small
            if (packet->packetno != packetCount) {
                fprintf(stderr, "Unexpected packet number %lld for header %d\n", 
                    packet->packetno, packetCount);
                return packetPcm;
            }

            int ret = vorbis_synthesis_headerin(&vi, &vc, packet);
            if (ret != 0) {
                fprintf(stderr, "Error processing Vorbis header %d: code %d\n", packetCount, ret);
                fprintf(stderr, "Packet size: %ld, B_O_S: %ld, packetno: %lld\n", 
                    packet->bytes, packet->b_o_s, packet->packetno);
                // Reset state on header failure
                headerParsed = false;
                packetCount = 0;
                return packetPcm;
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
                        fprintf(stderr, "Failed to initialize vorbis block: code %d\n", ret);
                        headerParsed = false;
                        packetCount = 0;
                    }
                } else {
                    fprintf(stderr, "Failed to initialize vorbis synthesis: code %d\n", ret);
                    headerParsed = false;
                    packetCount = 0;
                }
            }
            return packetPcm; // no PCM during header parsing
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

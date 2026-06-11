#if !defined(NO_XIPH_LIBS)

#include "ogg_flac_stream_decoder.h"
#include "../soloud_common.h"
#include <iostream>
#include <cstring> // For memcpy

OggFlacDecoderWrapper::OggFlacDecoderWrapper()
    : m_pFlacDecoder(nullptr),
      m_streamInfoProcessed(false),
      m_read_pos(0),
      m_streamInitialized(false),
      m_dataEnded(false),
      m_streamStartOffset(0),
      m_channels(0),
      m_samplerate(0),
      m_bitsPerSample(0),
      mIcyMetaInt(0),
      mAudioBytesCount(0),
      mIcyMetaSize(0)
{
}

OggFlacDecoderWrapper::~OggFlacDecoderWrapper()
{
    if (m_pFlacDecoder)
    {
        FLAC__stream_decoder_finish(m_pFlacDecoder);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
    }
    if (m_streamInitialized)
    {
        ogg_stream_clear(&m_os);
    }
    ogg_sync_clear(&m_oy);
}

void OggFlacDecoderWrapper::setIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt)
        return;

    mIcyMetaInt = icyMetaInt;
}

bool OggFlacDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    m_pFlacDecoder = FLAC__stream_decoder_new();
    if (m_pFlacDecoder == nullptr)
    {
        printf("[OggFlacDecoderWrapper] FLAC__stream_decoder_new failed\n");
        return false;
    }

    FLAC__stream_decoder_set_metadata_respond_all(m_pFlacDecoder);

    printf("[OggFlacDecoderWrapper] initializeDecoder called\n");
    m_streamStartOffset = 0;
    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_stream(
        m_pFlacDecoder,
        read_callback,
        nullptr, // seek_callback
        nullptr, // tell_callback
        nullptr, // length_callback
        nullptr, // eof_callback
        write_callback,
        metadata_callback,
        error_callback,
        this // client_data
    );

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        printf("[OggFlacDecoderWrapper] init_ogg_stream failed with status %d\n", init_status);
        return false;
    }

    printf("[OggFlacDecoderWrapper] decoder initialized OK\n");
    ogg_sync_init(&m_oy);
    return true;
}

std::pair<std::vector<float>, DecoderError> OggFlacDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate, int *channels)
{
    m_decodedPcm.clear();
    std::vector<unsigned char> clean_audio_data;

    if (mIcyMetaInt > 0)
    {
        for (unsigned char byte : buffer)
        {
            if (mIcyMetaSize > 0)
            {
                mIcyMetadata.push_back(byte);
                mIcyMetaSize--;
                if (mIcyMetaSize == 0)
                {
                    // Parse metadata
                    std::string meta(mIcyMetadata.begin(), mIcyMetadata.end());
                    size_t title_pos = meta.find("StreamTitle='");
                    if (title_pos != std::string::npos) {
                        size_t end_pos = meta.find("';", title_pos + 13);
                        if (end_pos != std::string::npos) {
                            std::string title = meta.substr(title_pos + 13, end_pos - (title_pos + 13));
                            if (title != mStreamTitle) {
                                mStreamTitle = title;
                                m_metadata.oggMetadata.comments["StreamTitle"] = title;
                                getMetadata();
                            }
                        }
                    }
                    mIcyMetadata.clear();
                }
            }
            else if (mAudioBytesCount == mIcyMetaInt)
            {
                mIcyMetaSize = byte * 16;
                mAudioBytesCount = 0;
            }
            else
            {
                clean_audio_data.push_back(byte);
                mAudioBytesCount++;
            }
        }
    }
    else
    {
        clean_audio_data = buffer;
    }

    printf("[OggFlacDecoderWrapper::decode] input buffer size=%zu, clean_audio_data size=%zu, m_audioData before=%zu\n",
           buffer.size(), clean_audio_data.size(), m_audioData.size());

    // Write new bytes into ogg sync buffer
    if (!clean_audio_data.empty())
    {
        char* oggBuffer = ogg_sync_buffer(&m_oy, clean_audio_data.size());
        memcpy(oggBuffer, clean_audio_data.data(), clean_audio_data.size());
        ogg_sync_wrote(&m_oy, clean_audio_data.size());
    }
    buffer.clear(); // Clear the original buffer as it has been processed

    // Extract pages and packets from Ogg container manually
    ogg_page og;
    while (ogg_sync_pageout(&m_oy, &og) == 1)
    {
        if (!m_streamInitialized)
        {
            if (ogg_stream_init(&m_os, ogg_page_serialno(&og)))
            {
                return {m_decodedPcm, DecoderError::FailedToCreateDecoder};
            }
            m_streamInitialized = true;
        }

        bool isNewStream = !m_streamInitialized || ogg_page_serialno(&og) != m_os.serialno;
        bool isBOS = ogg_page_bos(&og);

        if (isNewStream || isBOS)
        {
            if (m_streamInitialized)
            {
                ogg_stream_clear(&m_os);
            }
            if (ogg_stream_init(&m_os, ogg_page_serialno(&og)))
            {
                return {m_decodedPcm, DecoderError::FailedToCreateDecoder};
            }
            m_streamInitialized = true;
        }

        if (ogg_stream_pagein(&m_os, &og) < 0)
        {
            continue;
        }

        ogg_packet op;
        while (ogg_stream_packetout(&m_os, &op) == 1)
        {
            if (op.bytes > 0)
            {
                if (op.packetno == 0)
                {
                    // First packet (BOS) contains Ogg FLAC mapping headers.
                    // It starts with 0x7F 'FLAC', followed by version, header count,
                    // 'fLaC' signature, and STREAMINFO.
                    // We must skip the first 9 bytes of Ogg FLAC mapping headers
                    // to get the native FLAC signature ('fLaC') and STREAMINFO.
                    if (op.bytes >= 9 && op.packet[0] == 0x7F && memcmp(op.packet + 1, "FLAC", 4) == 0)
                    {
                        m_audioData.insert(m_audioData.end(), op.packet + 9, op.packet + op.bytes);
                    }
                    else
                    {
                        m_audioData.insert(m_audioData.end(), op.packet, op.packet + op.bytes);
                    }
                }
                else
                {
                    // Subsequent packets contain a native FLAC metadata block or frame.
                    m_audioData.insert(m_audioData.end(), op.packet, op.packet + op.bytes);
                }
            }
        }
    }

    unsigned int processCount = 0;
    size_t last_successful_read_pos = 0;
    while ((m_read_pos < m_audioData.size() || m_dataEnded) && processCount < 100000)
    {
        const size_t read_pos_before = m_read_pos;

        if (!FLAC__stream_decoder_process_single(m_pFlacDecoder))
        {
            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_pFlacDecoder);
            printf("[OggFlacDecoderWrapper::decode] process_single returned false at iter %u, read_pos_before=%zu, m_read_pos=%zu, state=%d\n",
                   processCount, read_pos_before, m_read_pos, state);
            if (state == FLAC__STREAM_DECODER_ABORTED)
            {
                // read_callback returned ABORT because the buffer is temporarily
                // exhausted but more data is expected. Roll back m_read_pos to
                // the last successful frame boundary, and flush the decoder so it
                // transitions to SEARCH_FOR_FRAME_SYNC and can resume on the
                // next decode() call.
                m_read_pos = last_successful_read_pos;
                FLAC__stream_decoder_flush(m_pFlacDecoder);
                printf("[OggFlacDecoderWrapper::decode] flushed decoder after temporary buffer exhaustion\n");
                break;
            }
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
            {
                printf("[OggFlacDecoderWrapper::decode] reached true end of stream\n");
                break;
            }
            // For other decoder errors, attempt recovery by flushing and rolling back
            m_read_pos = last_successful_read_pos;
            FLAC__stream_decoder_flush(m_pFlacDecoder);
            printf("[OggFlacDecoderWrapper::decode] decoder error state %d, recovered & breaking\n", state);
            break;
        }

        FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_pFlacDecoder);
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            printf("[OggFlacDecoderWrapper::decode] end of stream at iter %u\n", processCount);
            break;
        }

        // Query the exact byte position processed by the decoder (excluding lookahead FIFO)
        FLAC__uint64 absolute_pos = 0;
        if (FLAC__stream_decoder_get_decode_position(m_pFlacDecoder, &absolute_pos))
        {
            if (absolute_pos >= m_streamStartOffset)
            {
                last_successful_read_pos = absolute_pos - m_streamStartOffset;
            }
        }
        else
        {
            // Fallback during metadata phase
            last_successful_read_pos = m_read_pos;
        }
        processCount++;
    }

    printf("[OggFlacDecoderWrapper::decode] done - processCount=%u, m_read_pos=%zu/%zu, decodedPcm size=%zu, samplerate=%d, channels=%d\n",
           processCount, m_read_pos, m_audioData.size(), m_decodedPcm.size(), m_samplerate, m_channels);

    if (m_read_pos > 0)
    {
        m_streamStartOffset += m_read_pos;
        m_audioData.erase(m_audioData.begin(), m_audioData.begin() + m_read_pos);
    }
    m_read_pos = 0;

    *samplerate = m_samplerate;
    *channels = m_channels;

    return {m_decodedPcm, DecoderError::NoError};
}

FLAC__StreamDecoderReadStatus OggFlacDecoderWrapper::read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    OggFlacDecoderWrapper *self = static_cast<OggFlacDecoderWrapper *>(client_data);

    const size_t available_data = self->m_audioData.size() - self->m_read_pos;
    if (available_data == 0)
    {
        *bytes = 0;
        if (self->m_dataEnded)
        {
            // Stream has truly ended — no more data will arrive.
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
        // Buffer temporarily exhausted but more data is expected.
        // Return ABORT so process_single returns false without
        // permanently terminating the decoder.
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }

    const size_t bytes_to_copy = MIN(*bytes, available_data);
    memcpy(buffer, self->m_audioData.data() + self->m_read_pos, bytes_to_copy);
    self->m_read_pos += bytes_to_copy;

    printf("[OggFlacDecoderWrapper] read_callback requested=%zu returned=%zu read_pos=%zu/%zu\n",
           *bytes, bytes_to_copy, self->m_read_pos, self->m_audioData.size());

    *bytes = bytes_to_copy;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

void OggFlacDecoderWrapper::metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
    OggFlacDecoderWrapper *self = static_cast<OggFlacDecoderWrapper *>(client_data);
    printf("[OggFlacDecoderWrapper] metadata_callback type=%d\n", metadata->type);
    if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
    {
        self->m_metadata.oggMetadata.flacInfo.sample_rate = self->m_samplerate = metadata->data.stream_info.sample_rate;
        self->m_metadata.oggMetadata.flacInfo.channels = self->m_channels = metadata->data.stream_info.channels;
        self->m_metadata.oggMetadata.flacInfo.bits_per_sample = self->m_bitsPerSample = metadata->data.stream_info.bits_per_sample;
        self->m_metadata.oggMetadata.flacInfo.total_samples = (uint32_t)metadata->data.stream_info.total_samples;  
        self->m_metadata.oggMetadata.flacInfo.min_blocksize = metadata->data.stream_info.min_blocksize;
        self->m_metadata.oggMetadata.flacInfo.max_blocksize = metadata->data.stream_info.max_blocksize;
        self->m_metadata.oggMetadata.flacInfo.min_framesize = metadata->data.stream_info.min_framesize;
        self->m_metadata.oggMetadata.flacInfo.max_framesize = metadata->data.stream_info.max_framesize;
        self->m_streamInfoProcessed = true;
        printf("[OggFlacDecoderWrapper] STREAMINFO sr=%d ch=%d bps=%d total=%llu\n",
               self->m_samplerate, self->m_channels, self->m_bitsPerSample,
               (unsigned long long)metadata->data.stream_info.total_samples);
    }
    else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
    {
        self->m_metadata.type = DetectedType::BUFFER_OGG_FLAC;
        for (FLAC__uint32 i = 0; i < metadata->data.vorbis_comment.num_comments; ++i)
        {
            std::string comment(reinterpret_cast<const char *>(metadata->data.vorbis_comment.comments[i].entry));
            size_t equalsPos = comment.find('=');
            if (equalsPos != std::string::npos)
            {
                std::string key = comment.substr(0, equalsPos);
                std::string value = comment.substr(equalsPos + 1);
                if (key == "vendor")
                {
                    self->m_metadata.oggMetadata.vendor = value;
                }
                else
                {
                    self->m_metadata.oggMetadata.comments[key] = value;
                }
            }
        }
        self->m_metadata.oggMetadata.commentsCount = metadata->data.vorbis_comment.num_comments;
        self->getMetadata();
    }
}

void OggFlacDecoderWrapper::error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
    printf("[OggFlacDecoderWrapper] error_callback: FLAC decoder error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
    std::cerr << "FLAC decoder error: " << FLAC__StreamDecoderErrorStatusString[status] << std::endl;
}

FLAC__StreamDecoderWriteStatus OggFlacDecoderWrapper::write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
    OggFlacDecoderWrapper *self = static_cast<OggFlacDecoderWrapper *>(client_data);

    if (self->m_channels == 0)
    {
        self->m_channels = frame->header.channels;
    }
    if (self->m_samplerate == 0)
    {
        self->m_samplerate = frame->header.sample_rate;
    }

    size_t num_samples = frame->header.blocksize;
    unsigned int channels = frame->header.channels;
    unsigned int bps = frame->header.bits_per_sample;
    if (bps == 0) bps = self->m_bitsPerSample;
    if (bps == 0) bps = 16;

    float divisor = 1.0f;
    if (bps > 0 && bps < 32) {
        divisor = (float)(1u << (bps - 1));
    } else if (bps == 32) {
        divisor = 2147483648.0f;
    }

    for (size_t i = 0; i < num_samples; ++i)
    {
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            self->m_decodedPcm.push_back(static_cast<float>(buffer[channel][i]) / divisor);
        }
    }

    printf("[OggFlacDecoderWrapper] write_callback blocksize=%zu channels=%u bps=%u divisor=%f decodedPcm=%zu\n",
           num_samples, channels, bps, divisor, self->m_decodedPcm.size());

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void OggFlacDecoderWrapper::getMetadata()
{
    if (onTrackChange)
    {
        onTrackChange(m_metadata);
    }
}

#endif // #if !defined(NO_XIPH_LIBS)
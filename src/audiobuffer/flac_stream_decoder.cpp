#if !defined(NO_OPUS_OGG_LIBS)

#include "flac_stream_decoder.h"
#include "../common.h"
#include <iostream>
#include <cstring> // For memcpy

FlacDecoderWrapper::FlacDecoderWrapper()
    : m_pFlacDecoder(nullptr),
      m_streamInfoProcessed(false),
      m_streamInitialized(false),
      m_channels(0),
      m_samplerate(0),
      m_bitsPerSample(0),
      m_read_pos(0),
      mIcyMetaInt(0),
      mAudioBytesCount(0),
      mIcyMetaSize(0)
{
}

FlacDecoderWrapper::~FlacDecoderWrapper()
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

void FlacDecoderWrapper::setIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt)
        return;

    mIcyMetaInt = icyMetaInt;
}

bool FlacDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    m_pFlacDecoder = FLAC__stream_decoder_new();
    if (m_pFlacDecoder == nullptr)
    {
        return false;
    }

    FLAC__stream_decoder_set_metadata_respond_all(m_pFlacDecoder);

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
        return false;
    }

    ogg_sync_init(&m_oy);
    return true;
}

std::pair<std::vector<float>, DecoderError> FlacDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate, int *channels)
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

    // Now, use 'clean_audio_data' with the existing OGG/FLAC pipeline
    if (!clean_audio_data.empty())
    {
        char *ogg_buffer = ogg_sync_buffer(&m_oy, clean_audio_data.size());
        memcpy(ogg_buffer, clean_audio_data.data(), clean_audio_data.size());
        ogg_sync_wrote(&m_oy, clean_audio_data.size());
    }
    buffer.clear(); // Clear the original buffer as it has been processed

    while (ogg_sync_pageout(&m_oy, &m_og) == 1)
    {
        if (!m_streamInitialized)
        {
            if (ogg_stream_init(&m_os, ogg_page_serialno(&m_og)) != 0)
            {
                return {m_decodedPcm, DecoderError::FailedToCreateDecoder};
            }
            m_streamInitialized = true;
        }

        if (ogg_stream_pagein(&m_os, &m_og) < 0)
        {
            continue; // Skip corrupted page
        }

        while (ogg_stream_packetout(&m_os, &m_op) == 1)
        {
            m_audioData.insert(m_audioData.end(), m_op.packet, m_op.packet + m_op.bytes);
        }
    }

    m_read_pos = 0;
    while (m_read_pos < m_audioData.size())
    {
        const size_t read_pos_before = m_read_pos;

        if (!FLAC__stream_decoder_process_single(m_pFlacDecoder))
        {
            FLAC__stream_decoder_reset(m_pFlacDecoder);
            m_audioData.clear();
            m_read_pos = 0;
            break;
        }

        if (FLAC__stream_decoder_get_state(m_pFlacDecoder) == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            break;
        }

        if (m_read_pos == read_pos_before)
        {
            break;
        }
    }

    if (m_read_pos > 0)
    {
        m_audioData.erase(m_audioData.begin(), m_audioData.begin() + m_read_pos);
    }
    m_read_pos = 0;

    *samplerate = m_samplerate;
    *channels = m_channels;

    return {m_decodedPcm, DecoderError::NoError};
}

FLAC__StreamDecoderReadStatus FlacDecoderWrapper::read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    FlacDecoderWrapper *self = static_cast<FlacDecoderWrapper *>(client_data);

    const size_t available_data = self->m_audioData.size() - self->m_read_pos;
    if (available_data == 0)
    {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    const size_t bytes_to_copy = MIN(*bytes, available_data);
    memcpy(buffer, self->m_audioData.data() + self->m_read_pos, bytes_to_copy);
    self->m_read_pos += bytes_to_copy;

    *bytes = bytes_to_copy;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

void FlacDecoderWrapper::metadata_callback(const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
    FlacDecoderWrapper *self = static_cast<FlacDecoderWrapper *>(client_data);
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

void FlacDecoderWrapper::error_callback(const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
    std::cerr << "FLAC decoder error: " << FLAC__StreamDecoderErrorStatusString[status] << std::endl;
}

FLAC__StreamDecoderWriteStatus FlacDecoderWrapper::write_callback(const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
    FlacDecoderWrapper *self = static_cast<FlacDecoderWrapper *>(client_data);

    if (self->m_channels == 0)
    {
        self->m_channels = frame->header.channels;
    }
    if (self->m_samplerate == 0)
    {
        self->m_samplerate = frame->header.sample_rate;
    }

    size_t num_samples = frame->header.blocksize;
    float divisor = 1.0f;
    if (self->m_bitsPerSample == 16)
    {
        divisor = 32768.0f;
    }
    else if (self->m_bitsPerSample == 24)
    {
        divisor = 8388608.0f;
    }
    else if (self->m_bitsPerSample == 8)
    {
        divisor = 128.0f;
    }

    for (size_t i = 0; i < num_samples; ++i)
    {
        for (unsigned channel = 0; channel < self->m_channels; ++channel)
        {
            self->m_decodedPcm.push_back(static_cast<float>(buffer[channel][i]) / divisor);
        }
    }

    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

void FlacDecoderWrapper::getMetadata()
{
    if (onTrackChange)
    {
        onTrackChange(m_metadata);
    }
}

#endif // #if !defined(NO_OPUS_OGG_LIBS)
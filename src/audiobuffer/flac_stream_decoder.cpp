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
      m_bitsPerSample(0)
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

    char *ogg_buffer = ogg_sync_buffer(&m_oy, buffer.size());
    memcpy(ogg_buffer, buffer.data(), buffer.size());
    ogg_sync_wrote(&m_oy, buffer.size());
    buffer.clear();

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

    while (m_audioData.size() > 0)
    {
        size_t data_size_before = m_audioData.size();

        if (!FLAC__stream_decoder_process_single(m_pFlacDecoder))
        {
            break;
        }

        if (FLAC__stream_decoder_get_state(m_pFlacDecoder) == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            break;
        }

        if (m_audioData.size() == data_size_before)
        {
            break;
        }
    }

    *samplerate = m_samplerate;
    *channels = m_channels;

    return {m_decodedPcm, DecoderError::NoError};
}

FLAC__StreamDecoderReadStatus FlacDecoderWrapper::read_callback(const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    FlacDecoderWrapper *self = static_cast<FlacDecoderWrapper *>(client_data);

    if (self->m_audioData.empty())
    {
        *bytes = 0;
        return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
    }

    size_t bytes_to_copy = MIN(*bytes, self->m_audioData.size());
    memcpy(buffer, self->m_audioData.data(), bytes_to_copy);

    self->m_audioData.erase(self->m_audioData.begin(), self->m_audioData.begin() + bytes_to_copy);

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

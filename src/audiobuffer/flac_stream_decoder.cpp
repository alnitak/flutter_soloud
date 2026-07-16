#if !defined(NO_XIPH_LIBS)

#include "flac_stream_decoder.h"
#include "../soloud_common.h"
#include <iostream>
#include <cstring> // For memcpy

namespace
{
    /// Returns the offset of the next FLAC frame sync in the given data, or
    /// std::string::npos if not found.
    size_t findNextFlacFrame(const std::vector<unsigned char> &data, size_t start = 0)
    {
        for (size_t i = start; i + 1 < data.size(); ++i)
        {
            if (data[i] == 0xFF && (data[i + 1] & 0xFC) == 0xF8)
            {
                return i;
            }
        }
        return std::string::npos;
    }

    /// Returns the byte length of the FLAC header (magic + all metadata blocks)
    /// or 0 if the header is not yet complete in the provided buffer.
    size_t findFlacHeaderLength(const std::vector<unsigned char> &data)
    {
        if (data.size() < 4)
            return 0;
        if (data[0] != 'f' || data[1] != 'L' || data[2] != 'a' || data[3] != 'C')
            return 0; // not a FLAC file

        size_t pos = 4;
        while (true)
        {
            if (pos + 4 > data.size())
                return 0; // need more data

            const unsigned char blockType = data[pos];
            const size_t blockSize =
                (static_cast<size_t>(data[pos + 1]) << 16) |
                (static_cast<size_t>(data[pos + 2]) << 8) |
                static_cast<size_t>(data[pos + 3]);

            pos += 4 + blockSize;

            if (blockType & 0x80)
            {
                // Last-metadata-block flag set; audio frames follow.
                return pos;
            }
        }
    }
}

FlacDecoderWrapper::FlacDecoderWrapper()
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
    if (m_pFlacDecoder)
    {
        FLAC__stream_decoder_finish(m_pFlacDecoder);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
        m_pFlacDecoder = nullptr;
    }

    m_pFlacDecoder = FLAC__stream_decoder_new();
    if (m_pFlacDecoder == nullptr)
    {
        printf("[FlacDecoderWrapper] FLAC__stream_decoder_new failed\n");
        return false;
    }

    FLAC__stream_decoder_set_metadata_respond_all(m_pFlacDecoder);

    m_streamStartOffset = 0;
    FLAC__StreamDecoderInitStatus init_status = FLAC__stream_decoder_init_stream(
        m_pFlacDecoder,
        read_callback,
        nullptr, // seek_callback
        tell_callback,
        nullptr, // length_callback
        nullptr, // eof_callback
        write_callback,
        metadata_callback,
        error_callback,
        this // client_data
    );

    if (init_status != FLAC__STREAM_DECODER_INIT_STATUS_OK)
    {
        printf("[FlacDecoderWrapper] init_stream failed with status %d\n", init_status);
        return false;
    }

    printf("[FlacDecoderWrapper] decoder initialized OK\n");
    return true;
}

std::pair<std::vector<float>, DecoderError> FlacDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *sampleRate, int *channels, size_t maxOutputSamples)
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
    mTotalEncodedBytes += clean_audio_data.size();

    // Feed raw FLAC bytes directly to the decoder. Do NOT use Ogg parsing here;
    // this wrapper is for native FLAC files (magic 'fLaC').
    if (!clean_audio_data.empty())
    {
        m_audioData.insert(m_audioData.end(), clean_audio_data.begin(), clean_audio_data.end());
    }
    buffer.clear(); // Clear the original buffer as it has been processed

    // Capture the FLAC header on the first decode if we haven't already.
    if (mHeader.empty() && m_audioData.size() >= 4 &&
        m_audioData[0] == 'f' && m_audioData[1] == 'L' &&
        m_audioData[2] == 'a' && m_audioData[3] == 'C')
    {
        const size_t headerLen = findFlacHeaderLength(m_audioData);
        if (headerLen > 0)
        {
            mHeader.assign(m_audioData.begin(), m_audioData.begin() + headerLen);
            printf("[FlacDecoderWrapper] captured %zu header bytes\n", headerLen);
        }
        else if (m_pFlacDecoder == nullptr)
        {
            // Need the full header before we can initialize the decoder.
            return {{}, DecoderError::NoError};
        }
    }

    // If the decoder was reset (or hasn't been initialized yet), make sure the
    // buffer starts with the header so we can reinitialize. After a seek, the
    // chunk may begin mid-frame; trim the partial frame bytes before the first
    // FLAC frame boundary so the decoder can find sync.
    if (m_pFlacDecoder == nullptr)
    {
        if (mHeader.empty())
        {
            printf("[FlacDecoderWrapper] cannot initialize, no header captured\n");
            return {{}, DecoderError::NoError};
        }

        if (m_audioData.size() < 4 ||
            m_audioData[0] != 'f' || m_audioData[1] != 'L' ||
            m_audioData[2] != 'a' || m_audioData[3] != 'C')
        {
            // Find the first frame boundary in the seek chunk. If the chunk is
            // too small to contain a sync code, wait for more data.
            const size_t frameStart = findNextFlacFrame(m_audioData);
            if (frameStart == std::string::npos)
            {
                return {{}, DecoderError::NoError};
            }
            if (frameStart > 0)
            {
                printf("[FlacDecoderWrapper] trimming %zu bytes before first frame boundary\n",
                       frameStart);
                m_audioData.erase(m_audioData.begin(), m_audioData.begin() + frameStart);
            }

            // Prepend the previously captured header to the seek chunk.
            std::vector<unsigned char> withHeader;
            withHeader.reserve(mHeader.size() + m_audioData.size());
            withHeader.insert(withHeader.end(), mHeader.begin(), mHeader.end());
            withHeader.insert(withHeader.end(), m_audioData.begin(), m_audioData.end());
            m_audioData = std::move(withHeader);
        }

        if (!initializeDecoder(0, 0))
        {
            return {{}, DecoderError::FailedToCreateDecoder};
        }
    }

    m_read_pos = 0;
    unsigned int processCount = 0;
    size_t last_successful_read_pos = 0;
    while ((m_read_pos < m_audioData.size() || m_dataEnded) && processCount < 100000)
    {
        if (!FLAC__stream_decoder_process_single(m_pFlacDecoder))
        {
            FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_pFlacDecoder);
            if (state == FLAC__STREAM_DECODER_ABORTED)
            {
                // read_callback returned ABORT because the buffer is temporarily
                // exhausted but more data is expected. Roll back m_read_pos to
                // the last successful frame boundary, and flush the decoder so it
                // transitions to SEARCH_FOR_FRAME_SYNC and can resume on the
                // next decode() call.
                m_read_pos = last_successful_read_pos;
                FLAC__stream_decoder_flush(m_pFlacDecoder);
                printf("[FlacDecoderWrapper::decode] flushed decoder after temporary buffer exhaustion\n");
                break;
            }
            if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
            {
                break;
            }
            // For other decoder errors, attempt recovery by flushing and rolling back
            m_read_pos = last_successful_read_pos;
            FLAC__stream_decoder_flush(m_pFlacDecoder);
            break;
        }

        FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_pFlacDecoder);
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
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

        if (maxOutputSamples > 0 && m_decodedPcm.size() >= maxOutputSamples)
        {
            break;
        }
    }

    if (m_read_pos > 0)
    {
        m_streamStartOffset += m_read_pos;
        m_audioData.erase(m_audioData.begin(), m_audioData.begin() + m_read_pos);
    }
    m_read_pos = 0;

    *sampleRate = m_samplerate;
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

    *bytes = bytes_to_copy;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderTellStatus FlacDecoderWrapper::tell_callback(const FLAC__StreamDecoder *decoder, FLAC__uint64 *absolute_byte_offset, void *client_data)
{
    FlacDecoderWrapper *self = static_cast<FlacDecoderWrapper *>(client_data);
    *absolute_byte_offset = self->m_streamStartOffset + self->m_read_pos;
    return FLAC__STREAM_DECODER_TELL_STATUS_OK;
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
        self->mTotalSamples = metadata->data.stream_info.total_samples;  
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

    const size_t frameTotalSamples = num_samples * self->m_channels;
    const uint64_t frameStartSample =
        static_cast<uint64_t>(frame->header.number.sample_number) * self->m_channels;

    size_t startSample = 0;
    if (self->mPendingSkipTargetSample > 0 &&
        frameStartSample < self->mPendingSkipTargetSample)
    {
        if (frameStartSample + frameTotalSamples <= self->mPendingSkipTargetSample)
        {
            // Whole frame is before the seek target; skip it entirely.
            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        }
        startSample = static_cast<size_t>(self->mPendingSkipTargetSample - frameStartSample);
    }

    for (size_t sample = startSample; sample < frameTotalSamples; ++sample)
    {
        const unsigned channel = sample % self->m_channels;
        const size_t i = sample / self->m_channels;
        self->m_decodedPcm.push_back(static_cast<float>(buffer[channel][i]) / divisor);
    }

    if (frameStartSample + frameTotalSamples >= self->mPendingSkipTargetSample)
    {
        self->mPendingSkipTargetSample = 0;
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

bool FlacDecoderWrapper::canSeekToTime(double seconds) const
{
    return m_samplerate > 0 && mTotalSamples > 0 && seconds > 0.0;
}

uint64_t FlacDecoderWrapper::timeToByteOffset(double seconds)
{
    if (m_samplerate == 0 || mTotalSamples == 0 || seconds <= 0.0)
        return 0;
    const double totalSeconds = static_cast<double>(mTotalSamples) / static_cast<double>(m_samplerate);
    if (totalSeconds <= 0.0)
        return 0;
    const double ratio = seconds / totalSeconds;
    const uint64_t totalBytes = mTotalAudioSizeBytes > 0 ? mTotalAudioSizeBytes : mTotalEncodedBytes;
    if (totalBytes == 0)
        return 0;
    return static_cast<uint64_t>(ratio * static_cast<double>(totalBytes));
}

double FlacDecoderWrapper::getDuration() const
{
    if (m_samplerate == 0 || mTotalSamples == 0)
        return -1.0;
    return static_cast<double>(mTotalSamples) / static_cast<double>(m_samplerate);
}

void FlacDecoderWrapper::prepareForSeek(uint64_t targetSample)
{
    if (m_pFlacDecoder)
    {
        FLAC__stream_decoder_finish(m_pFlacDecoder);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
        m_pFlacDecoder = nullptr;
    }
    m_audioData.clear();
    m_read_pos = 0;
    m_streamStartOffset = 0;
    m_streamInfoProcessed = false;
    mTotalEncodedBytes = 0;
    m_decodedPcm.clear();
    m_dataEnded = false;
    mPendingSkipTargetSample = targetSample;
    printf("[FlacDecoderWrapper] prepareForSeek targetSample=%llu\n",
           static_cast<unsigned long long>(targetSample));
}

#endif // #if !defined(NO_XIPH_LIBS)
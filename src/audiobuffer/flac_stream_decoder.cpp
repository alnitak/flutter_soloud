#if !defined(NO_XIPH_LIBS)

#include "flac_stream_decoder.h"
#include "metadata_bounds.h"
#include "../soloud_common.h"
#include "../simd/simd_pcm.h"
#include <algorithm>
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
            if (!metadataHasBytes(data.size(), pos, 4))
                return 0; // need more data

            const unsigned char blockType = data[pos];
            const size_t blockSize =
                (static_cast<size_t>(data[pos + 1]) << 16) |
                (static_cast<size_t>(data[pos + 2]) << 8) |
                static_cast<size_t>(data[pos + 3]);

            // A delivery can end anywhere inside the metadata. The header is
            // complete only once every byte of every block has arrived.
            if (!metadataHasBytes(data.size(), pos + 4, blockSize))
                return 0;
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
      m_dataEnded(false),
      m_drained(false),
      m_streamStartOffset(0),
      m_channels(0),
      m_samplerate(0),
      m_bitsPerSample(0),
      mIcyMetaInt(0)
{
}

FlacDecoderWrapper::~FlacDecoderWrapper()
{
    if (m_pFlacDecoder)
    {
        FLAC__stream_decoder_finish(m_pFlacDecoder);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
    }
}

void FlacDecoderWrapper::setIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt)
        return;

    mIcyMetaInt = icyMetaInt;
}

bool FlacDecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    (void)engineSamplerate;
    (void)engineChannels;
    m_drained = false;
    m_read_pos = 0;
    if (m_pFlacDecoder)
    {
        FLAC__stream_decoder_finish(m_pFlacDecoder);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
        m_pFlacDecoder = nullptr;
    }

    m_pFlacDecoder = FLAC__stream_decoder_new();
    if (m_pFlacDecoder == nullptr)
    {
        SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] FLAC__stream_decoder_new failed\n");
        return false;
    }

    FLAC__stream_decoder_set_metadata_respond_all(m_pFlacDecoder);

    m_streamStartOffset = 0;
    m_decodedUpTo = 0;
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
        SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] init_stream failed with status %d\n", init_status);
        FLAC__stream_decoder_delete(m_pFlacDecoder);
        m_pFlacDecoder = nullptr;
        return false;
    }

    SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] decoder initialized OK\n");
    return true;
}

std::pair<std::vector<float>, DecoderError> FlacDecoderWrapper::decode(std::vector<unsigned char> &buffer, int *sampleRate, int *channels, size_t maxOutputSamples)
{
    m_decodedPcm.clear();

    // Strip ICY metadata when present; otherwise use the input chunk as-is,
    // avoiding an intermediate copy.
    std::vector<unsigned char> clean_audio_data;
    const std::vector<unsigned char> *audioBytes = &buffer;
    if (mIcyMetaInt > 0)
    {
        stripIcyMetadata(buffer, mIcyMetaInt, mIcy, clean_audio_data,
                         [this](const std::string &title) {
                             m_metadata.oggMetadata.comments["StreamTitle"] = title;
                             getMetadata();
                         });
        audioBytes = &clean_audio_data;
    }
    mTotalEncodedBytes += audioBytes->size();

    // Feed raw FLAC bytes directly to the decoder. Do NOT use Ogg parsing here;
    // this wrapper is for native FLAC files (magic 'fLaC').
    if (!audioBytes->empty())
    {
        m_audioData.insert(m_audioData.end(), audioBytes->begin(), audioBytes->end());
    }
    buffer.clear(); // Clear the original buffer as it has been processed

    // The format and the header are checked on the first 4 bytes: wait for
    // them, or libFLAC would start reading the metadata from a partial
    // delivery and abort.
    if (mHeader.empty() && m_audioData.size() < 4)
    {
        if (m_dataEnded)
        {
            const bool empty = m_audioData.empty();
            m_audioData.clear();
            m_read_pos = 0;
            m_drained = true;
            return {{}, empty ? DecoderError::NoError : DecoderError::CodecError};
        }
        return {{}, DecoderError::NoError};
    }

    // Capture the FLAC header on the first decode if we haven't already.
    if (mHeader.empty() &&
        m_audioData[0] == 'f' && m_audioData[1] == 'L' &&
        m_audioData[2] == 'a' && m_audioData[3] == 'C')
    {
        const size_t headerLen = findFlacHeaderLength(m_audioData);
        if (headerLen > 0)
        {
            mHeader.assign(m_audioData.begin(), m_audioData.begin() + headerLen);
            SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] captured %zu header bytes\n", headerLen);
        }
        else if (m_dataEnded)
        {
            // The data ended inside the metadata, so the header can never
            // complete. Drop the input and end the stream, or a drain loop
            // waiting on hasPendingData() would wait forever.
            m_audioData.clear();
            m_read_pos = 0;
            m_drained = true;
            return {{}, DecoderError::CodecError};
        }
        else
        {
            // Wait for the rest of the header even when StreamDecoder has
            // already initialized the codec: once the codec consumes these
            // bytes the header can no longer be cached, and a seek restarts
            // the codec from the cached header.
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
            SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] cannot initialize, no header captured\n");
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
                SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] trimming %zu bytes before first frame boundary\n",
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

    // libFLAC buffers input in its bit reader, so m_read_pos can be ahead of
    // what it has decoded. When the read callback aborts for lack of data,
    // libFLAC is left ABORTED with a partial block in the bit reader: roll the
    // read position back to the end of the last decoded block and flush, so
    // the next call re-reads that block from its start once more data is
    // there. process_single() returns true or false on an abort, depending on
    // where the read happened, so check the state in both cases.
    auto rollBackAndFlush = [this]() {
        m_read_pos = m_decodedUpTo >= m_streamStartOffset
                         ? static_cast<size_t>(m_decodedUpTo - m_streamStartOffset)
                         : 0;
        if (m_read_pos > m_audioData.size())
            m_read_pos = m_audioData.size();
        FLAC__stream_decoder_flush(m_pFlacDecoder);
    };

    unsigned int processCount = 0;
    while ((m_read_pos < m_audioData.size() || m_dataEnded) && processCount < 100000)
    {
        const bool ok = FLAC__stream_decoder_process_single(m_pFlacDecoder);
        const FLAC__StreamDecoderState state = FLAC__stream_decoder_get_state(m_pFlacDecoder);
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM)
        {
            m_drained = true;
            break;
        }
        if (!ok || state == FLAC__STREAM_DECODER_ABORTED)
        {
            // ABORTED: the buffer is temporarily exhausted but more data is
            // expected. Other errors: attempt recovery the same way.
            rollBackAndFlush();
            // Once the data has ended reads no longer abort, so this is a
            // fatal error: end the stream, or a drain loop would never end.
            if (m_dataEnded && state != FLAC__STREAM_DECODER_ABORTED)
                m_drained = true;
            SOLOUD_DEBUG_LOG("[FlacDecoderWrapper::decode] flushed decoder (state %d)\n", (int)state);
            break;
        }

        // The exact byte position decoded so far (excluding the bit reader).
        FLAC__uint64 absolute_pos = 0;
        if (FLAC__stream_decoder_get_decode_position(m_pFlacDecoder, &absolute_pos))
            m_decodedUpTo = absolute_pos;
        else
            m_decodedUpTo = m_streamStartOffset + m_read_pos;
        processCount++;

        if (maxOutputSamples > 0 && m_decodedPcm.size() >= maxOutputSamples)
        {
            break;
        }
    }

    // Drop the bytes libFLAC has decoded. Bytes it has only read into its bit
    // reader stay, since a later abort rolls back to them.
    size_t decodedRel = m_decodedUpTo > m_streamStartOffset
                            ? static_cast<size_t>(m_decodedUpTo - m_streamStartOffset)
                            : 0;
    if (decodedRel > m_read_pos)
        decodedRel = m_read_pos;
    if (decodedRel > 0)
    {
        if (decodedRel >= m_audioData.size())
        {
            m_streamStartOffset += decodedRel;
            m_audioData.clear();
            m_read_pos = 0;
        }
        else if (decodedRel >= 65536 && decodedRel * 2 >= m_audioData.size())
        {
            m_streamStartOffset += decodedRel;
            m_audioData.erase(m_audioData.begin(), m_audioData.begin() + decodedRel);
            m_read_pos -= decodedRel;
        }
    }

    *sampleRate = m_samplerate;
    *channels = m_channels;

    return {std::move(m_decodedPcm), DecoderError::NoError};
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
    SOLOUD_DEBUG_LOG("FLAC decoder error: %s\n", FLAC__StreamDecoderErrorStatusString[status]);
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

    const size_t num_samples = frame->header.blocksize;
    const unsigned channels = static_cast<unsigned>(self->m_channels);
    unsigned int bps = frame->header.bits_per_sample;
    if (bps == 0) bps = static_cast<unsigned int>(self->m_bitsPerSample);
    if (bps == 0) bps = 16;
    const float scale = (bps > 0 && bps < 32)
                            ? (1.0f / static_cast<float>(1u << (bps - 1)))
                            : ((bps == 32) ? (1.0f / 2147483648.0f) : 1.0f);

    // The output has the STREAMINFO channel count (reported to the caller
    // and used for seeking). A frame with another count is mapped to it: its
    // last channel is repeated, or its extra channels are dropped. libFLAC
    // gives only the frame's channel buffers.
    const unsigned frameChannels = frame->header.channels;
    if (frameChannels == 0 || channels > FLAC__MAX_CHANNELS)
    {
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }
    const FLAC__int32 *planes[FLAC__MAX_CHANNELS];
    for (unsigned c = 0; c < channels; ++c)
    {
        planes[c] = buffer[std::min(c, frameChannels - 1)];
    }

    const size_t frameTotalSamples = num_samples * channels;
    const uint64_t frameStartSample =
        static_cast<uint64_t>(frame->header.number.sample_number) * channels;

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

    if (startSample == 0 && num_samples > 0 && channels > 0)
    {
        const size_t oldSize = self->m_decodedPcm.size();
        self->m_decodedPcm.resize(oldSize + frameTotalSamples);
        SoLoud::interleavePlanarInt32ToFloats(
            reinterpret_cast<const int32_t *const *>(planes),
            self->m_decodedPcm.data() + oldSize,
            static_cast<unsigned int>(num_samples), channels, scale);
    }
    else if (channels > 0)
    {
        // Partial first frame after seek: de-interleave from startSample.
        self->m_decodedPcm.reserve(self->m_decodedPcm.size() +
                                   (frameTotalSamples - startSample));
        const size_t startFrame = startSample / channels;
        const unsigned startChannel = static_cast<unsigned>(startSample % channels);
        for (size_t i = startFrame; i < num_samples; ++i)
        {
            const unsigned firstChannel = (i == startFrame) ? startChannel : 0;
            for (unsigned channel = firstChannel; channel < channels; ++channel)
            {
                self->m_decodedPcm.push_back(static_cast<float>(planes[channel][i]) * scale);
            }
        }
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

void FlacDecoderWrapper::prepareForSeek(uint64_t targetSample, uint64_t byteOffset)
{
    (void)byteOffset; // Offsets are estimated from the file size.
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
    m_drained = false;
    m_decodedUpTo = 0;
    mPendingSkipTargetSample = targetSample;
    SOLOUD_DEBUG_LOG("[FlacDecoderWrapper] prepareForSeek targetSample=%llu\n",
           static_cast<unsigned long long>(targetSample));
}

bool FlacDecoderWrapper::hasPendingData() const
{
    if (m_dataEnded && m_drained)
    {
        return false;
    }
    if (m_read_pos < m_audioData.size())
    {
        return true;
    }
    if (m_dataEnded && !m_drained)
    {
        return true;
    }
    return false;
}

#endif // #if !defined(NO_XIPH_LIBS)
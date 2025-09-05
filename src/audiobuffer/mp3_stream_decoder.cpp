#include "mp3_stream_decoder.h"
#include "../common.h"

// #define DR_MP3_IMPLEMENTATION
#include "../soloud/src/audiosource/wav/dr_mp3.h"


size_t MP3DecoderWrapper::on_read(void* pUserData, void* pBufferOut, size_t bytesToRead) {
    MP3DecoderWrapper* decoder = (MP3DecoderWrapper*)pUserData;
    size_t bytes_remaining = decoder->audioData.size() - decoder->m_read_pos;
    size_t bytes_to_copy = (bytesToRead > bytes_remaining) ? bytes_remaining : bytesToRead;

    if (bytes_to_copy > 0) {
        memcpy(pBufferOut, decoder->audioData.data() + decoder->m_read_pos, bytes_to_copy);
        decoder->m_read_pos += bytes_to_copy;
    }

    return bytes_to_copy;
}

drmp3_bool32 MP3DecoderWrapper::on_seek(void* pUserData, int offset, drmp3_seek_origin origin) {
    MP3DecoderWrapper* decoder = (MP3DecoderWrapper*)pUserData;
    size_t new_pos = decoder->m_read_pos;

    if (origin == DRMP3_SEEK_SET) {
        new_pos = offset;
    } else { //drmp3_seek_origin_current
        new_pos += offset;
    }

    if (new_pos > decoder->audioData.size()) {
        return DRMP3_FALSE; // Seek out of bounds
    }

    decoder->m_read_pos = new_pos;
    return DRMP3_TRUE;
}

MP3DecoderWrapper::MP3DecoderWrapper()
    : isInitialized(false),
      audioData({}),
      m_read_pos(0),
      bytes_until_meta(16000), // most common value
      metadata_buffer(""),
      lastMetadata(""),
      mIcyMetaInt(0),
      ID3TagsFound(false)
{
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
    drmp3_uninit(&decoder);
}

void MP3DecoderWrapper::setMp3BufferIcyMetaInt(int icyMetaInt)
{
    if (mIcyMetaInt == icyMetaInt)
        return;

    mIcyMetaInt = icyMetaInt;
    bytes_until_meta = mIcyMetaInt;
}

bool MP3DecoderWrapper::extractID3Tags(const std::vector<unsigned char> &buffer, AudioMetadata &metadata)
{
    // Look for ID3v2 tag. ID3v1 is not supported (metadata is at the end of the file)
    if (buffer.size() > 10 && memcmp(buffer.data(), "ID3", 3) == 0)
    {
        size_t pos = 10; // Skip ID3v2 header
        uint32_t size = ((buffer[6] & 0x7f) << 21) |
                        ((buffer[7] & 0x7f) << 14) |
                        ((buffer[8] & 0x7f) << 7) |
                        (buffer[9] & 0x7f);

        while (pos < size + 10 && pos + 10 < buffer.size())
        {
            char frame_id[5] = {0};
            memcpy(frame_id, buffer.data() + pos, 4);
            uint32_t frame_size = ((buffer[pos + 4] & 0x7f) << 21) |
                                  ((buffer[pos + 5] & 0x7f) << 14) |
                                  ((buffer[pos + 6] & 0x7f) << 7) |
                                  (buffer[pos + 7] & 0x7f);

            pos += 10; // Skip frame header
            if (pos + frame_size > buffer.size())
                break;

            // Skip text encoding byte for text frames
            size_t text_start = (frame_id[0] == 'T') ? 1 : 0;
            std::string value(reinterpret_cast<const char *>(buffer.data() + pos + text_start), frame_size - text_start);
            if (frame_id[0] != 0)
                printf("ID3 tag: %s = %s\n", frame_id, value.c_str());

            if (strcmp(frame_id, "TIT2") == 0)      metadata.mp3Metadata.title = value;
            else if (strcmp(frame_id, "TPE1") == 0) metadata.mp3Metadata.artist = value;
            else if (strcmp(frame_id, "TALB") == 0) metadata.mp3Metadata.album = value;
            else if (strcmp(frame_id, "TYER") == 0) metadata.mp3Metadata.date = value;
            else if (strcmp(frame_id, "TCON") == 0) metadata.mp3Metadata.genre = value;

            pos += frame_size;
        }
        metadata.type = BUFFER_MP3_WITH_ID3;
        return true;
    }
    return false;
}

// A helper function to check for a valid MP3 header.
// This is a simplified version for demonstration.
// dr_mp3 does this internally, but we might need it for finding frame boundaries when dealing with ICY metadata.
bool hdr_valid(const uint8_t *hdr) {
    if (hdr[0] != 0xFF || (hdr[1] & 0xE0) != 0xE0) {
        return false; // Sync word check
    }
    // Further checks for bitrate, sample rate, etc., can be added here.
    return true;
}


size_t MP3DecoderWrapper::getLastFrameStartingPos(std::vector<unsigned char> &buffer, size_t *bytes_discarded_at_end)
{
    const uint8_t *mp3_ptr = buffer.data();
    size_t buffer_size = buffer.size();

    if (buffer_size < 4) // An MP3 frame header is 4 bytes. 
    {
        *bytes_discarded_at_end = buffer_size;
        return 0;
    }

    // Iterate backwards from the last possible header position.
    for (size_t i = buffer_size - 4; ; i--)
    {
        if (hdr_valid(mp3_ptr + i))
        {
            // Found a valid header. This is the start of the last potential frame.
            *bytes_discarded_at_end = buffer_size - i;
            return i;
        }
        if (i == 0)
        {
            // We've checked the start of the buffer and found no header.
            break;
        }
    }

    // No valid header found in the buffer.
    *bytes_discarded_at_end = buffer_size;
    return 0;
}

// Return a vector of char containing only valid MP3 frames stripped of metadata and of the last truncated frame
std::vector<unsigned char> MP3DecoderWrapper::checkIcyMeta(
    std::vector<unsigned char> &buffer,
    size_t *bytes_discarded_at_end) // these number of bytes should the only to be kept in the buffer
{
    std::vector<unsigned char> onlyFrames;
    if (detectedType != DetectedType::BUFFER_MP3_STREAM || mIcyMetaInt == 0)
    {
        // Not a stream with metadata, just find the last frame and return the rest
        size_t pos = getLastFrameStartingPos(buffer, bytes_discarded_at_end);
        if (pos > 0)
        {
            onlyFrames.insert(onlyFrames.end(), buffer.begin(), buffer.begin() + pos);
        }
        return onlyFrames;
    }

    size_t bufferSize = buffer.size();
    size_t readingPos = 0;

    while (readingPos < bufferSize)
    {
        size_t bytes_to_read = std::min((size_t)bytes_until_meta, bufferSize - readingPos);

        // Append audio data
        onlyFrames.insert(onlyFrames.end(), buffer.begin() + readingPos, buffer.begin() + readingPos + bytes_to_read);
        readingPos += bytes_to_read;
        bytes_until_meta -= bytes_to_read;

        if (bytes_until_meta == 0)
        {
            // Time for metadata
            if (readingPos < bufferSize)
            {
                int len_byte = buffer[readingPos];
                int metadata_len = len_byte * 16;

                readingPos++; // Skip metadata length byte

                if (readingPos + metadata_len <= bufferSize)
                {
                    if (len_byte > 0)
                    {
                        // Extract and process metadata
                        std::string title(reinterpret_cast<const char *>(buffer.data() + readingPos), metadata_len);

                        if (lastMetadata != title)
                        {
                            AudioMetadata metadata;
                            metadata.type = DetectedType::BUFFER_MP3_STREAM;
                            metadata.mp3Metadata.title = title;
                            lastMetadata = title;
                            if (onTrackChange)
                                onTrackChange(metadata);
                        }
                    }
                    readingPos += metadata_len;
                    bytes_until_meta = mIcyMetaInt;
                }
                else
                {
                    // Not enough data for the full metadata block, so we stop here for this chunk
                    // The remaining (incomplete) metadata will be handled in the next chunk
                    readingPos--; // Rewind to include the metadata length byte in the next processing round
                    break;
                }
            }
        }
    }

    // The buffer for the next round should start where we left off.
    // The main `decode` function will handle keeping the rest of the buffer.
    *bytes_discarded_at_end = buffer.size() - readingPos;

    // Now, from the collected audio frames, find the last complete frame
    size_t last_frame_bytes_discarded = 0;
    size_t last_frame_pos = getLastFrameStartingPos(onlyFrames, &last_frame_bytes_discarded);

    if (last_frame_pos > 0)
    {
        // Trim the incomplete frame from the end
        onlyFrames.resize(last_frame_pos);
    }
    else
    {
        // No complete frame found in this chunk
        onlyFrames.clear();
    }

    // The total discarded bytes at the end of the original buffer needs to account for
    // the incomplete frame data we just trimmed.
    *bytes_discarded_at_end += last_frame_bytes_discarded;

    // When an incomplete frame is carried over, it's still audio data that counts
    // towards the next metadata block. Adjust bytes_until_meta accordingly.
    bytes_until_meta += last_frame_bytes_discarded;

    return onlyFrames;
}


std::pair<std::vector<float>, DecoderError> MP3DecoderWrapper::decode(std::vector<unsigned char> &buffer, int *samplerate, int *channels)
{
    if (buffer.empty())
        return {{}, DecoderError::NoError};

    *samplerate = -1;
    *channels = -1;

    // With MP3 with ID3 TAG, the metadata is extracted with the extractID3Tags function
    if (detectedType == DetectedType::BUFFER_MP3_WITH_ID3 && !ID3TagsFound)
    {
        // Check for new metadata
        AudioMetadata newMetadata;
        if (extractID3Tags(buffer, newMetadata))
        {
            ID3TagsFound = true;
            if (onTrackChange)
                onTrackChange(newMetadata);
        }
    }
    if (ID3TagsFound)
    {
        size_t bytes_discarded_at_end = 0;
        getLastFrameStartingPos(buffer, &bytes_discarded_at_end);
        audioData.insert(audioData.end(), buffer.begin(), buffer.end() - bytes_discarded_at_end);
        // Remove bytes_discarded_at_end from the end of the buffer
        buffer.erase(buffer.begin(), buffer.end() - bytes_discarded_at_end);
    }

    // With MP3 stream, the metadata is stored inside the stream every [mIcyMetaInt] bytes.
    if (detectedType == DetectedType::BUFFER_MP3_STREAM && !ID3TagsFound)
    {
        size_t bytes_discarded_at_end = 0;
        std::vector<unsigned char> newAudioData = checkIcyMeta(buffer, &bytes_discarded_at_end);
        audioData.insert(audioData.end(), newAudioData.begin(), newAudioData.end());
        // Leave only bytes_discarded_at_end bytes at the end of the buffer
        buffer.erase(buffer.begin(), buffer.end() - bytes_discarded_at_end);
    }

    if (!isInitialized)
    {
        struct TempData {
            const unsigned char* buffer;
            size_t size;
            size_t pos;
        };
        TempData temp_data = { audioData.data(), audioData.size(), 0 };

        if (!drmp3_init(&decoder, MP3DecoderWrapper::on_read, MP3DecoderWrapper::on_seek, nullptr, nullptr, &temp_data, nullptr))
            return {{}, DecoderError::NoError};
        isInitialized = true;
    }

    // finally decode audioData
    std::vector<float> decodedData;
    m_read_pos = 0;

    const int MAX_FRAMES_PER_RUN = 4096;
    float pcm_frames[MAX_FRAMES_PER_RUN];
    drmp3_uint64 frames_read;

    do {
        frames_read = drmp3_read_pcm_frames_f32(&decoder, MAX_FRAMES_PER_RUN / decoder.channels, pcm_frames);
        if (frames_read > 0) {
            decodedData.insert(decodedData.end(), pcm_frames, pcm_frames + frames_read * decoder.channels);
        }
    } while (frames_read > 0);


    *samplerate = decoder.sampleRate;
    *channels = decoder.channels;

    // After decoding, clear the audioData that has been processed.
    // The on_read callback uses m_read_pos to read through audioData.
    // We can erase the part of audioData that has been read.
    if (m_read_pos > 0) {
        audioData.erase(audioData.begin(), audioData.begin() + m_read_pos);
        m_read_pos = 0;
    }


    return {decodedData, DecoderError::NoError};
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    // if (!drmp3_init(&decoder, MP3DecoderWrapper::on_read, MP3DecoderWrapper::on_seek, nullptr, nullptr, this, nullptr)) {
    //     return false;
    // }
    return true;
}

bool MP3DecoderWrapper::checkForValidFrames(const std::vector<unsigned char> &buffer)
{
    // Heuristic: If the buffer is too small to possibly contain two frames, fail early.
    if (buffer.size() < 8) {
        return false;
    }

    drmp3 temp_decoder;
    struct TempData {
        const unsigned char* buffer;
        size_t size;
        size_t pos;
    };
    TempData temp_data = { buffer.data(), buffer.size(), 0 };

    auto temp_on_read = [](void* pUserData, void* pBufferOut, size_t bytesToRead) -> size_t {
        TempData* data = (TempData*)pUserData;
        if (bytesToRead == 0) return 0;

        size_t bytes_left = data->size - data->pos;
        if (bytes_left == 0) return 0;

        size_t bytes_to_copy = (bytesToRead > bytes_left) ? bytes_left : bytesToRead;
        memcpy(pBufferOut, data->buffer + data->pos, bytes_to_copy);
        data->pos += bytes_to_copy;
        return bytes_to_copy;
    };

    if (!drmp3_init(&temp_decoder, temp_on_read, nullptr, nullptr, nullptr, &temp_data, nullptr)) {
        return false;
    }

    // Try to read 2 frames. We pass NULL for the buffer as we only want to check for validity.
    drmp3_uint64 frames_read = drmp3_read_pcm_frames_f32(&temp_decoder, 2, nullptr);

    drmp3_uninit(&temp_decoder);

    return frames_read >= 2;
}
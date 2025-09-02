// TODO: don't know why, bu on iOS and MacOS these defines should be put in the header!
#include "../common.h"
#if !defined(__APPLE__)
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_ONLY_MP3
#if !defined(__EMSCRIPTEN__)
#define MINIMP3_ONLY_SIMD
#endif
#define MINIMP3_IMPLEMENTATION
#endif
#include "minimp3.h"

#include "../common.h"
#include "mp3_stream_decoder.h"

MP3DecoderWrapper::MP3DecoderWrapper()
    : audioData({}),
      bytes_until_meta(16000), // most common value
      metadata_remaining(0),
      metadata_buffer(""),
      lastMetadata(""),
      mIcyMetaInt(0),
      validFramesFound(false),
      ID3TagsFound(false)
{
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
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
        size_t bytes_to_read = MIN((size_t)bytes_until_meta, bufferSize - readingPos);

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

    // Wait at least 2 valid mp3 frames to start decoding if we haven't found them yet
    int bytes_left = buffer.size();
    if (!validFramesFound && bytes_left > 0)
    {
        int validFrames = 0;
        const uint8_t *frame_ptr = buffer.data();
        int remaining = bytes_left;

        while (remaining >= 4 && validFrames < 2) // Need at least 4 bytes for header check
        {
            if (hdr_valid(frame_ptr))
            {
                validFrames++;
                // Skip to next possible frame using current frame's length
                mp3dec_frame_info_t temp_info;
                mp3d_sample_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_decode_frame(&decoder, frame_ptr, remaining, temp_pcm, &temp_info);
                if (temp_info.frame_bytes <= 0)
                    break;
                frame_ptr += temp_info.frame_bytes;
                remaining -= temp_info.frame_bytes;
                *samplerate = temp_info.hz;
                *channels = temp_info.channels;
            }
            else
            {
                frame_ptr++;
                remaining--;
            }
        }

        if (validFrames >= 2)
        {
            validFramesFound = true;
        }
        else
        {
            // Not enough valid frames yet, keep the buffer for next time
            return {{}, DecoderError::NoError};
        }
    }

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
    // Here [audioBuffer] is the original [buffer] stripped of the metadata and ready
    // to be decoded by minimp3.
    // If a frame has been truncated at the end of [buffer], don't include it
    // in the [audioBuffer]. In the [audioBuffer] there should be only valid mp3 frames. This is what [checkIcyMeta] does.
    // The [buffer] should be cleared of decoded frames and metadata.
    if (detectedType == DetectedType::BUFFER_MP3_STREAM && !ID3TagsFound)
    {
        size_t bytes_discarded_at_end = 0;
        audioData = checkIcyMeta(buffer, &bytes_discarded_at_end);
        // Leave only bytes_discarded_at_end bytes at the end of the buffer
        buffer.erase(buffer.begin(), buffer.end() - bytes_discarded_at_end);
    }

    // finally decode audioData
    std::vector<float> decodedData;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    const uint8_t *mp3_ptr = audioData.data();
    mp3dec_frame_info_t frame_info;
    int decoded_samples = 0;
    bytes_left = audioData.size();

    while (bytes_left > 0)
    {
        int samples = mp3dec_decode_frame(&decoder, mp3_ptr, bytes_left, pcm, &frame_info);

        if (samples)
        {
            decodedData.insert(decodedData.end(), pcm, pcm + samples * frame_info.channels);
            mp3_ptr += frame_info.frame_bytes;
            bytes_left -= frame_info.frame_bytes;
            *samplerate = frame_info.hz;
            *channels = frame_info.channels;
        }
        else
        {
            // Error or not enough data for a full frame
            if (frame_info.frame_bytes == 0) // No frame found, break to avoid infinite loop
            {
                mp3_ptr++;
                bytes_left--;
                continue;
            }
            // If frame_info.frame_bytes > 0 but samples == 0, it means an error occurred
            // or not enough data for a full frame. We should advance to avoid getting stuck.
            mp3_ptr += frame_info.frame_bytes;
            bytes_left -= frame_info.frame_bytes;
        }
    }

    audioData.clear();

    return {decodedData, DecoderError::NoError};
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    mp3dec_init(&decoder);
    return true;
}

bool MP3DecoderWrapper::checkForValidFrames(const std::vector<unsigned char> &buffer)
{
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    int validFrames = 0;
    mp3dec_t decoder;

    mp3dec_init(&decoder);
    if (bytes_left > 0)
    {
        const uint8_t *frame_ptr = mp3_ptr;
        int remaining = bytes_left;

        while (remaining >= 4 && validFrames < 2) // Need at least 4 bytes for header check
        {
            if (hdr_valid(frame_ptr))
            {
                validFrames++;
                // Skip to next possible frame using current frame's length
                mp3dec_frame_info_t temp_info;
                mp3d_sample_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_decode_frame(&decoder, frame_ptr, remaining, temp_pcm, &temp_info);
                if (temp_info.frame_bytes <= 0)
                    break;
                frame_ptr += temp_info.frame_bytes;
                remaining -= temp_info.frame_bytes;
            }
            else
            {
                frame_ptr++;
                remaining--;
            }
        }
    }
    return validFrames >= 2;
}

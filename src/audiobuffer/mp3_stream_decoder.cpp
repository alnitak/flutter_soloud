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

#include "mp3_stream_decoder.h"

MP3DecoderWrapper::MP3DecoderWrapper()
    : isInitialized(false),
    validFramesFound(false)
{	
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
}

void MP3DecoderWrapper::cleanup()
{
    isInitialized = false;
    validFramesFound = false;
}

bool MP3DecoderWrapper::extractID3Tags(const std::vector<unsigned char>& buffer, AudioMetadata& metadata) {
    // Look for ID3v2 tag
    // if (buffer.size() > 10 && memcmp(buffer.data(), "ID3", 3) == 0) {
    //     size_t pos = 10;  // Skip ID3v2 header
    //     uint32_t size = ((buffer[6] & 0x7f) << 21) | 
    //                    ((buffer[7] & 0x7f) << 14) |
    //                    ((buffer[8] & 0x7f) << 7) |
    //                    (buffer[9] & 0x7f);
        
    //     while (pos < size + 10 && pos + 10 < buffer.size()) {
    //         char frame_id[5] = {0};
    //         memcpy(frame_id, buffer.data() + pos, 4);
    //         uint32_t frame_size = ((buffer[pos+4] & 0x7f) << 21) |
    //                             ((buffer[pos+5] & 0x7f) << 14) |
    //                             ((buffer[pos+6] & 0x7f) << 7) |
    //                             (buffer[pos+7] & 0x7f);
            
    //         pos += 10;  // Skip frame header
    //         if (pos + frame_size > buffer.size())
    //             break;

    //         // Skip text encoding byte for text frames
    //         size_t text_start = (frame_id[0] == 'T') ? 1 : 0;
    //         std::string value(reinterpret_cast<const char *>(buffer.data() + pos + text_start), frame_size - text_start);

    //         if (strcmp(frame_id, "TIT2") == 0) metadata.title = value;
    //         else if (strcmp(frame_id, "TPE1") == 0) metadata.artist = value;
    //         else if (strcmp(frame_id, "TALB") == 0) metadata.album = value;
    //         else if (strcmp(frame_id, "TYER") == 0) metadata.date = value;
    //         else if (strcmp(frame_id, "TCON") == 0) metadata.genre = value;
    //         else metadata.additionalTags[frame_id] = value;
            
    //         pos += frame_size;
    //     }
    //     return true;
    // }
    return false;
}

std::pair<std::vector<float>, DecoderError> MP3DecoderWrapper::decode(std::vector<unsigned char>& buffer, int* samplerate, int* channels)
{
    if (buffer.empty())
        return {{}, DecoderError::NoError};
        
    // Check for new metadata
    AudioMetadata newMetadata;
    
    // if (extractID3Tags(buffer, newMetadata)) {
    //     // Compare with last metadata to detect changes
    //     if (newMetadata.title != lastMetadata.title || 
    //         newMetadata.artist != lastMetadata.artist ||
    //         newMetadata.album != lastMetadata.album) {
    //         lastMetadata = newMetadata;
    //         if (onTrackChange) {
    //             onTrackChange(newMetadata);
    //         }
    //     }
    // }

    std::vector<float> decodedData;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t frame_info;
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    *samplerate = -1;
    *channels = -1;

    // Wait at least 2 valid mp3 frames to start decoding if we haven't found them yet
    if (!validFramesFound && bytes_left > 0) {
        int validFrames = 0;
        const uint8_t *frame_ptr = mp3_ptr;
        int remaining = bytes_left;

         while (remaining >= 4 && validFrames < 2) {  // Need at least 4 bytes for header check
            if (hdr_valid(frame_ptr))
            {
                validFrames++;
                // Skip to next possible frame using current frame's length
                mp3dec_frame_info_t temp_info;
                mp3d_sample_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_decode_frame(&decoder, frame_ptr, remaining, temp_pcm, &temp_info);
                if (temp_info.frame_bytes <= 0) break;
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

        if (validFrames >= 2) {
            validFramesFound = true;
        } else {
            // Not enough valid frames yet, keep the buffer for next time
            return {};
        }
    }


    while (bytes_left > 0)
    {
        int samples = mp3dec_decode_frame(&decoder, mp3_ptr, bytes_left, pcm, &frame_info);

        if (samples)
        {
            decodedData.insert(decodedData.end(), pcm, pcm + samples * frame_info.channels);
            mp3_ptr += frame_info.frame_bytes;
            bytes_left -= frame_info.frame_bytes;
        } else {
            // Error or not enough data for a full frame
            if (frame_info.frame_bytes == 0) { // No frame found, break to avoid infinite loop
                break;
            }
            // If frame_info.frame_bytes > 0 but samples == 0, it means an error occurred
            // or not enough data for a full frame. We should advance to avoid getting stuck.
            mp3_ptr += frame_info.frame_bytes;
            bytes_left -= frame_info.frame_bytes;
        }
    }
    // Keep any remaining bytes in the buffer for the next call
    buffer.erase(buffer.begin(), buffer.end() - bytes_left);

    return {decodedData, DecoderError::NoError};
}

bool MP3DecoderWrapper::initializeDecoder(int engineSamplerate, int engineChannels)
{
    cleanup(); // Ensure clean state
    mp3dec_init(&decoder);
    isInitialized = true;
    return true;
}

bool MP3DecoderWrapper::checkForValidFrames(const std::vector<unsigned char>& buffer)
{
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    int validFrames = 0;
    mp3dec_t decoder;

    mp3dec_init(&decoder);
    if (bytes_left > 0) {
        const uint8_t *frame_ptr = mp3_ptr;
        int remaining = bytes_left;

         while (remaining >= 4 && validFrames < 2) {  // Need at least 4 bytes for header check
            if (hdr_valid(frame_ptr))
            {
                validFrames++;
                // Skip to next possible frame using current frame's length
                mp3dec_frame_info_t temp_info;
                mp3d_sample_t temp_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
                mp3dec_decode_frame(&decoder, frame_ptr, remaining, temp_pcm, &temp_info);
                if (temp_info.frame_bytes <= 0) break;
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

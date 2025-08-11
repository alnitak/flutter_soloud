#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_ONLY_MP3
#define MINIMP3_ONLY_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "mp3_stream_decoder.h"

MP3DecoderWrapper::MP3DecoderWrapper()
    : isInitialized(false),
      validFramesFound(false)
{
}

MP3DecoderWrapper::~MP3DecoderWrapper()
{
    cleanup();
}

void MP3DecoderWrapper::cleanup()
{
    isInitialized = false;
    validFramesFound = false;
}

std::vector<float> MP3DecoderWrapper::decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels)
{
    if (buffer.empty())
        return {};

    std::vector<float> decodedData;
    mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    mp3dec_frame_info_t frame_info;
    int bytes_left = buffer.size();
    const uint8_t *mp3_ptr = buffer.data();
    *sampleRate = -1;
    *channels = -1;

    if (!isInitialized)
    {
        initializeDecoder();
        isInitialized = true;
    }

    // Wait at least 2 valid mp3 frames to start decoding if we haven't found them yet
    if (!validFramesFound && bytes_left > 0) {
        int validFrames = 0;
        const uint8_t *frame_ptr = mp3_ptr;
        int remaining = bytes_left;

         while (remaining >= 4 && validFrames < 2) {  // Need at least 4 bytes for header check
            // printf("Valid: %d  Remaining: %d\n", validFrames, remaining);
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
                *sampleRate = temp_info.hz;
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
            printf("************** decoded %d samples\n", samples);
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

    return decodedData;
}

bool MP3DecoderWrapper::initializeDecoder()
{
    cleanup(); // Ensure clean state
    mp3dec_init(&decoder);
    return true;
}

#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

// dr_mp3 wants us to define this before including the header
// #define DR_MP3_IMPLEMENTATION
#ifndef dr_mp3_h
#include "../soloud/src/audiosource/wav/dr_mp3.h"
#endif

/// Wrapper class for MP3 stream decoder using dr_mp3
class MP3DecoderWrapper
{
public:
    MP3DecoderWrapper(int sampleRate, int channels)
        : targetSampleRate(static_cast<drmp3_uint32>(sampleRate)),
          targetChannels(static_cast<drmp3_uint32>(channels)),
          isInitialized(false)
    {
    }

    ~MP3DecoderWrapper()
    {
        cleanup();
    }

    void cleanup()
    {
        isInitialized = false;
    }

    std::vector<float> decode(const unsigned char *inputData, size_t inputSize, size_t *decodedBytes)
    {
        *decodedBytes = 0;
        if (inputSize == 0)
            return {};

        // Add new data to the buffer
        buffer.insert(buffer.end(), inputData, inputData + inputSize);

        if (!isInitialized)
        {
            if (!initializeDecoder())
            {
                // If initialization fails, it might be because there's not enough data yet.
                // Keep the data in the buffer for the next call.
                return {};
            }
        }

        std::vector<float> decodedData;
        const size_t MAX_FRAMES_PER_CALL = 1152 * 4; // Adjust as needed
        std::vector<float> tempBuffer(MAX_FRAMES_PER_CALL * targetChannels);

        size_t totalFramesDecoded = 0;

        drmp3_uint64 framesDecoded = drmp3_read_pcm_frames_f32(&decoder, MAX_FRAMES_PER_CALL, tempBuffer.data());
        if (framesDecoded > 0)
        {
            decodedData.insert(decodedData.end(), tempBuffer.begin(), tempBuffer.begin() + framesDecoded * targetChannels);
            totalFramesDecoded += framesDecoded;
        }

        // This is a bit of a hack, but since we can't know the exact bytes consumed from the stream,
        // we'll clear the buffer and assume all of it was used. This is acceptable because the `onRead`
        // callback will provide the data again if needed.
        *decodedBytes = buffer.size();
        buffer.clear();

        return decodedData;
    }

private:
    static size_t onRead(void *pUserData, void *pBufferOut, size_t bytesToRead)
    {
        MP3DecoderWrapper *self = static_cast<MP3DecoderWrapper *>(pUserData);
        if (self->buffer.empty())
        {
            return 0;
        }

        size_t bytesToCopy = std::min(bytesToRead, self->buffer.size());
        memcpy(pBufferOut, self->buffer.data(), bytesToCopy);

        // Here we have a choice, either remove the data from the buffer or not.
        // Let's not remove it, and let the main decode loop handle it.
        // self->buffer.erase(self->buffer.begin(), self->buffer.begin() + bytesToCopy);

        return bytesToCopy;
    }

    static void on_meta(void *pUserData, const drmp3_metadata *pMetadata)
    {
        const char *pMetaName = "Unknown";
        if (pMetadata->type == DRMP3_METADATA_TYPE_ID3V1)
        {
            pMetaName = "ID3v1";
        }
        else if (pMetadata->type == DRMP3_METADATA_TYPE_ID3V2)
        {
            pMetaName = "ID3v2";
        }
        else if (pMetadata->type == DRMP3_METADATA_TYPE_APE)
        {
            pMetaName = "APE";
        }
        else if (pMetadata->type == DRMP3_METADATA_TYPE_XING)
        {
            pMetaName = "Xing";
        }
        else if (pMetadata->type == DRMP3_METADATA_TYPE_VBRI)
        {
            pMetaName = "Info";
        }

        printf("Metadata: %s (%d bytes)\n", pMetaName, (int)pMetadata->rawDataSize);

        (void)pUserData;
    }

    bool initializeDecoder()
    {
        cleanup(); // Ensure clean state

        if (!drmp3_init(&decoder, onRead, nullptr, nullptr, on_meta, this, nullptr))
        {
            return false;
        }

        isInitialized = true;
        return true;
    }

private:
    drmp3 decoder;
    drmp3_uint32 targetSampleRate;
    drmp3_uint32 targetChannels;
    bool isInitialized;
    std::vector<unsigned char> buffer;
};

#endif // MP3_STREAM_DECODER_H

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

    std::vector<float> decode(std::vector<unsigned char> buffer, size_t *decodedBytes)
    {
        *decodedBytes = 0;
        if (buffer.empty())
            return {};

        mBuffer = &buffer;

        if (!isInitialized)
        {
            if (!initializeDecoder())
            {
                // If initialization fails, it might be because there's not enough data yet.
                // Keep the data in the buffer for the next call.
                return {};
            }
            dataStart = 0;
            dataLength = 0;
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
        *decodedBytes = decoder.streamCursor;

        return decodedData;
    }

    bool mustCompactBuffer() {
        if (dataStart > 1024 * 32) {
            mBuffer->erase(mBuffer->begin(), mBuffer->begin() + dataStart);
            dataStart = 0;
            // dataLength = mBuffer->size();
        }
    }

private:
    static size_t onRead(void *pUserData, void *pBufferOut, size_t bytesToRead)
    {
        MP3DecoderWrapper *self = static_cast<MP3DecoderWrapper *>(pUserData);
        if (self->mBuffer->empty())
        {
            return 0;
        }

        size_t toRead = std::min(bytesToRead, self->mBuffer->size());
        memcpy(pBufferOut, self->mBuffer->data(), toRead);

        self->dataStart += toRead;
        self->dataLength -= toRead;

        return toRead;
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

        MP3DecoderWrapper *self = static_cast<MP3DecoderWrapper *>(pUserData);
        printf("Metadata: %s (%d bytes)\n", pMetaName, (int)pMetadata->rawDataSize);
        printf("Metadata streamStartOffset: %d\n", self->decoder.streamStartOffset);

        (void)pUserData;
    }

    bool initializeDecoder()
    {
        cleanup(); // Ensure clean state

        dataStart = 0;
        dataLength = 0;
        
        if (!drmp3_init(&decoder, onRead, nullptr, nullptr, on_meta, this, nullptr))
        {
            return false;
        }

        isInitialized = true;
        return true;
    }

public:
    drmp3 decoder;
private:
    drmp3_uint32 targetSampleRate;
    drmp3_uint32 targetChannels;
    bool isInitialized;
    std::vector<unsigned char> *mBuffer;
    size_t dataStart;
    size_t dataLength;
};

#endif // MP3_STREAM_DECODER_H

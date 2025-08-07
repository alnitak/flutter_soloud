#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

// #if !defined(DR_MP3_IMPLEMENTATION)
// #   define DR_MP3_IMPLEMENTATION
// #endif
// #ifndef dr_mp3_h
// #   include "../soloud/src/audiosource/wav/dr_mp3.h"
// #include "../soloud/src/audiosource/wav/dr_impl.cpp"

// #endif



#include "./soloud/src/audiosource/wav/dr_mp3.h"

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
        drmp3_uninit(&decoder);
    }

    void cleanup()
    {
        isInitialized = false;
    }

    std::vector<float> decode(std::vector<unsigned char>& buffer)
    {
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
        }

        std::vector<float> decodedData;
        int maxFramesPerCall = buffer.size() / sizeof(float) / decoder.channels;
        std::vector<float> tempBuffer(maxFramesPerCall * targetChannels);

        size_t totalFramesDecoded = 0;

        drmp3_uint64 framesDecoded = drmp3_read_pcm_frames_f32(&decoder, maxFramesPerCall, tempBuffer.data());
        if (framesDecoded > 0)
        {
            decodedData.insert(decodedData.end(), tempBuffer.begin(), tempBuffer.begin() + framesDecoded * targetChannels);
            totalFramesDecoded += framesDecoded;
            countFramesInBuffer(buffer, framesDecoded);
            cleanupBuffer(buffer);
        }

        return decodedData;
    }

    void countFramesInBuffer(std::vector<unsigned char>& buffer, drmp3_uint64 framesDecoded)
    {
        // Start from the beginning of the buffer to count valid frames
        drmp3_uint64 counter = 0;
        for (int i = 0; i < static_cast<int>(buffer.size()) - 1; i++)
        {
            const drmp3_uint8 *h = &buffer[i];
            if (h[0] == 0xff &&
                ((h[1] & 0xF0) == 0xf0 || (h[1] & 0xFE) == 0xe2) &&
                ((((h[1]) >> 1) & 3) != 0) &&
                (((h[2]) >> 4) != 15) &&
                ((((h[2]) >> 2) & 3) != 3)) {
                counter++;
                // if (counter >= framesDecoded-1) {
                //     buffer.erase(buffer.begin(), buffer.begin() + i);
                //     break;
                // }
            }
        }
        printf("********************** framesDecoded %d  counter %d   buffer size %d\n", framesDecoded, counter, buffer.size());
    }

    /// Find the last MP3 frame start and leave it into the buffer.
    /// When other data is added, the MP3 decoder will start decoding from here.
    void cleanupBuffer(std::vector<unsigned char>& buffer)
    {
        int frameStart = findLastMP3FrameStart(buffer);
        if (frameStart >= 0 && static_cast<size_t>(frameStart) < buffer.size()) {
            printf("*** buffer size before: %d   ", buffer.size());
            // Keep only from the last frame onward
            // if (drmp3_hdr_frame_bytes(&buffer.at(frameStart), buffer.size() - frameStart) > buffer.size() - frameStart) {
                buffer.erase(buffer.begin(), buffer.begin() + frameStart);
            // }
            printf("*** buffer size AFTER: %d\n", buffer.size());
        }
    }

private:
    int findLastMP3FrameStart(std::vector<unsigned char>& buffer) {
        if (buffer.size() < 2) return -1;

        for (int i = static_cast<int>(buffer.size()) - 2; i >= 0; --i) {
            const drmp3_uint8 *h = &buffer[i];
            // inlining drmp3_hdr_valid()
            if (h[0] == 0xff &&
                ((h[1] & 0xF0) == 0xf0 || (h[1] & 0xFE) == 0xe2) &&
                ((((h[1]) >> 1) & 3) != 0) &&
                (((h[2]) >> 4) != 15) &&
                ((((h[2]) >> 2) & 3) != 3)) {
                return i;
            }
        }
        return -1;
    }

    static size_t onRead(void *pUserData, void *pBufferOut, size_t bytesToRead)
    {
        MP3DecoderWrapper *self = static_cast<MP3DecoderWrapper *>(pUserData);
        if (self->mBuffer->empty())
        {
            return 0;
        }

        size_t toRead = std::min(bytesToRead, self->mBuffer->size());
        memcpy(pBufferOut, self->mBuffer->data(), toRead);

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
};

#endif // MP3_STREAM_DECODER_H

#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

// dr_mp3 wants us to define this before including the header
// #define DR_MP3_IMPLEMENTATION
#include "../soloud/src/audiosource/wav/dr_mp3.h"

#define DRMP3_MAX_FRAME_SIZE 1441 // Maximum possible frame size for MP3

/// Wrapper class for MP3 stream decoder using dr_mp3
class MP3DecoderWrapper {
public:
    MP3DecoderWrapper(int sampleRate, int channels)
        : targetSampleRate(static_cast<drmp3_uint32>(sampleRate)), 
          targetChannels(static_cast<drmp3_uint32>(channels)), 
          isInitialized(false),
          currentFrameSize(0),
          minRequiredBytes(2048),  // Start with reasonable minimum
          isVBR(false)
    {
        previousFrameSizes.resize(10, 0);
        printf("MP3DecoderWrapper initialized with sampleRate=%d, channels=%d\n", sampleRate, channels);
    }

    ~MP3DecoderWrapper() {
        cleanup();
    }

    void cleanup() {
        if (isInitialized) {
            drmp3_uninit(&decoder);
            isInitialized = false;
        }
        std::fill(previousFrameSizes.begin(), previousFrameSizes.end(), 0);
        currentFrameSize = 0;
        minRequiredBytes = 0;
    }

    // Returns minimum bytes needed for next decode
    size_t getMinimumBytesNeeded() const {
        return minRequiredBytes;
    }

    bool isVariableBitrate() const {
        return isVBR;
    }

    std::vector<float> decode(const unsigned char* inputData, size_t inputSize) {
        std::vector<float> decodedData;

        // Print first few bytes for debugging
        printf("Input data starts with: ");
        for (size_t i = 0; i < std::min(size_t(16), inputSize); i++) {
            printf("%02X ", inputData[i]);
        }
        printf("\n");
        
        // First time or reinitialization
        if (!isInitialized) {
            try {
                if (!initializeDecoder(inputData, inputSize)) {
                    printf("Failed to initialize decoder\n");
                    return decodedData;
                }
            } catch (const std::exception& e) {
                printf("Decoder initialization error: %s\n", e.what());
                return decodedData;
            }
        }

        // Pre-allocate buffer for efficiency
        std::vector<float> tempBuffer(2304);  // 1152 samples * 2 channels
        size_t totalBytesProcessed = 0;
        drmp3dec_frame_info info = {};

        while (totalBytesProcessed + 4 <= inputSize) {
            float* output = tempBuffer.data();
            
            // Try to decode a frame
            int samplesPerChannel = drmp3dec_decode_frame(
                &decoder.decoder,
                inputData + totalBytesProcessed,
                inputSize - totalBytesProcessed,
                output,
                &info
            );

            if (samplesPerChannel > 0) {
                size_t totalSamples = samplesPerChannel * info.channels;
                printf("Decoded frame: samples=%d, bytes=%d, ch=%d, sr=%d\n",
                       samplesPerChannel, info.frame_bytes, info.channels, info.sample_rate);
                
                // Add decoded samples to output
                decodedData.insert(decodedData.end(), 
                                 output,
                                 output + totalSamples);

                // Update tracking
                updateFrameSizeTracking(info.frame_bytes);
                totalBytesProcessed += info.frame_bytes;
            } else {
                // Try to find next sync word
                if (totalBytesProcessed + 2 <= inputSize) {
                    const unsigned char* next = inputData + totalBytesProcessed;
                    if ((next[0] == 0xFF) && ((next[1] & 0xE0) == 0xE0)) {
                        printf("Found new sync word at offset %zu\n", totalBytesProcessed);
                        // Try reinitializing at new sync point
                        cleanup();
                        if (initializeDecoder(next, inputSize - totalBytesProcessed)) {
                            totalBytesProcessed = 0;
                            continue;
                        }
                    }
                }
                totalBytesProcessed++;
            }
        }

        return decodedData;
    }

private:
    bool initializeDecoder(const unsigned char* data, size_t dataSize) {
        cleanup();

        if (dataSize < 4) {
            printf("Not enough data for MP3 header (%zu bytes)\n", dataSize);
            return false;
        }

        // Find sync word
        const unsigned char* syncPos = data;
        size_t remaining = dataSize;
        bool foundSync = false;
        
        while (remaining >= 4) {
            if ((syncPos[0] == 0xFF) && ((syncPos[1] & 0xE0) == 0xE0)) {
                // Verify MP3 header
                unsigned char version = (syncPos[1] >> 3) & 0x03;
                unsigned char layer = (syncPos[1] >> 1) & 0x03;
                unsigned char bitrate = (syncPos[2] >> 4) & 0x0F;
                unsigned char sampleRate = (syncPos[2] >> 2) & 0x03;
                
                if (layer != 0 && bitrate != 0x0F && sampleRate != 0x03) {
                    foundSync = true;
                    break;
                }
            }
            syncPos++;
            remaining--;
        }

        if (!foundSync) {
            printf("No valid MP3 header found in %zu bytes\n", dataSize);
            return false;
        }

        size_t offset = syncPos - data;
        printf("Found MP3 sync at offset %zu. Header: %02X %02X %02X %02X\n",
               offset, syncPos[0], syncPos[1], syncPos[2], syncPos[3]);

        // Initialize decoder
        drmp3_bool32 result = drmp3_init_memory(&decoder, syncPos, remaining, nullptr);
        if (!result) {
            printf("drmp3_init_memory failed\n");
            return false;
        }

        printf("Decoder initialized: sr=%u, ch=%u\n", decoder.sampleRate, decoder.channels);
        
        if (decoder.channels > 2) {
            cleanup();
            printf("Unsupported channel count: %u\n", decoder.channels);
            return false;
        }

        isInitialized = true;
        currentFrameSize = 0;  // Will be set after first successful decode
        return true;
    }

    void updateFrameSizeTracking(size_t newFrameSize) {
        if (newFrameSize == 0) return;

        if (currentFrameSize == 0) {
            currentFrameSize = newFrameSize;
            minRequiredBytes = newFrameSize;
            std::fill(previousFrameSizes.begin(), previousFrameSizes.end(), newFrameSize);
            return;
        }
        
        // Update frame size history
        previousFrameSizes.pop_front();
        previousFrameSizes.push_back(newFrameSize);

        // Check for VBR
        size_t firstSize = previousFrameSizes.front();
        isVBR = false;
        for (size_t size : previousFrameSizes) {
            if (size != firstSize) {
                isVBR = true;
                break;
            }
        }

        // Update minimum required bytes
        if (isVBR) {
            size_t maxSize = 0;
            for (size_t size : previousFrameSizes) {
                maxSize = std::max(maxSize, size);
            }
            minRequiredBytes = maxSize;
        } else {
            minRequiredBytes = newFrameSize;
        }
        
        currentFrameSize = newFrameSize;
    }

private:
    drmp3 decoder;
    drmp3_uint32 targetSampleRate;
    drmp3_uint32 targetChannels;
    bool isInitialized;
    size_t currentFrameSize;
    size_t minRequiredBytes;
    bool isVBR;
    std::deque<size_t> previousFrameSizes;
};

#endif // MP3_STREAM_DECODER_H

#ifndef MP3_STREAM_DECODER_H
#define MP3_STREAM_DECODER_H

#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <deque>

// dr_mp3 wants us to define this before including the header
#define DR_MP3_IMPLEMENTATION
#include "../soloud/src/audiosource/wav/dr_mp3.h"

/// Wrapper class for MP3 stream decoder using dr_mp3
class MP3DecoderWrapper {
public:
    MP3DecoderWrapper(int sampleRate, int channels)
        : targetSampleRate(static_cast<drmp3_uint32>(sampleRate)), 
          targetChannels(static_cast<drmp3_uint32>(channels)), 
          isInitialized(false),
          currentFrameSize(0),
          minRequiredBytes(0),
          isVBR(false)
    {
        // Store some frames to detect VBR
        previousFrameSizes.resize(10, 0);
    }

    ~MP3DecoderWrapper() {
        cleanup();
    }

    void cleanup() {
        if (isInitialized) {
            drmp3_uninit(&decoder);
            isInitialized = false;
        }
        previousFrameSizes.clear();
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

        // First time or reinitialization
        if (!isInitialized) {
            if (!initializeDecoder(inputData, inputSize)) {
                return decodedData;
            }
        }

        // We need enough data for at least one frame
        if (inputSize < minRequiredBytes) {
            return decodedData;
        }

        // Pre-allocate buffer for efficiency (typical MP3 frame is 1152 samples per channel)
        std::vector<float> tempBuffer(4608); // Max 2 channels * 1152 samples * 2 frames
        decodedData.reserve(4608);

        size_t totalBytesProcessed = 0;
        drmp3dec_frame_info frameInfo;
        
        while (totalBytesProcessed < inputSize) {
            // Try to decode next frame
            drmp3_uint64 framesDecoded = drmp3_read_pcm_frames_f32(
                &decoder, 
                1152,  // Request one frame worth of samples
                tempBuffer.data()
            );

            if (framesDecoded == 0) {
                // No more complete frames in this chunk
                break;
            }

            // Get frame info from dr_mp3
            drmp3dec_frame_info info;
            drmp3dec_decode_frame(&decoder.decoder, inputData + totalBytesProcessed, 
                                inputSize - totalBytesProcessed, nullptr, &info);
            
            // Update frame size tracking for VBR detection
            size_t newFrameSize = info.frame_bytes;
            updateFrameSizeTracking(newFrameSize);

            // Add decoded samples to output
            size_t samplesDecoded = framesDecoded * decoder.channels;
            decodedData.insert(decodedData.end(),
                             tempBuffer.begin(),
                             tempBuffer.begin() + samplesDecoded);

            totalBytesProcessed += newFrameSize;
        }

        // Update minimum required bytes based on recent frame sizes
        updateMinRequiredBytes();

        return decodedData;
    }

private:
    bool initializeDecoder(const unsigned char* data, size_t dataSize) {
        cleanup(); // Ensure clean state

        if (!drmp3_init_memory(&decoder, data, dataSize, nullptr)) {
            throw std::runtime_error("Failed to initialize MP3 decoder");
        }

        // Verify format compatibility
        if (decoder.channels > 2) {
            cleanup();
            throw std::runtime_error("Unsupported channel count");
        }

        isInitialized = true;
        
        // Get initial frame info
        drmp3dec_frame_info info;
        drmp3dec_decode_frame(&decoder.decoder, data, dataSize, nullptr, &info);
        
        currentFrameSize = info.frame_bytes;
        minRequiredBytes = currentFrameSize;

        // Initialize frame size tracking
        std::fill(previousFrameSizes.begin(), previousFrameSizes.end(), currentFrameSize);

        return true;
    }

    void updateFrameSizeTracking(size_t newFrameSize) {
        // Shift previous sizes and add new one
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
    }

    void updateMinRequiredBytes() {
        if (isVBR) {
            // For VBR, use the maximum frame size seen recently
            size_t maxSize = 0;
            for (size_t size : previousFrameSizes) {
                maxSize = std::max(maxSize, size);
            }
            minRequiredBytes = maxSize;
        } else {
            // For CBR, use the constant frame size
            minRequiredBytes = currentFrameSize;
        }
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

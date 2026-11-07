#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <mutex>

#include "../enums.h"
#include "../simd/simd_pcm.h"

enum BufferingType
{
    PRESERVED,
    RELEASED
};

class Buffer
{
public:
    std::vector<int8_t> buffer; // Buffer that stores int8_t data
    BufferingType bufferingType;

private:
    size_t maxBytes; // Maximum capacity in bytes
    size_t readOffset; // Byte offset to active data (used in RELEASED mode)

    void compactLocked(size_t bytesNeeded) {
        if (readOffset > 0 &&
            ((buffer.size() - readOffset) < readOffset ||
             buffer.size() + bytesNeeded > maxBytes)) {
            const size_t remaining = buffer.size() - readOffset;
            if (remaining > 0) {
                std::memmove(buffer.data(), buffer.data() + readOffset, remaining);
            }
            buffer.resize(remaining);
            readOffset = 0;
        }
    }

public:
    std::recursive_mutex bufferMutex; // Add mutex for thread safety

    size_t getReadOffset() const { return readOffset; }

    size_t getActiveSizeInBytes() const {
        if (buffer.size() <= readOffset) return 0;
        return buffer.size() - readOffset;
    }

    // Constructor that accepts the maxBytes parameter
    Buffer() : bufferingType(BufferingType::PRESERVED), maxBytes(0), readOffset(0) {}

    ~Buffer()
    {
        clear();
    }

    void setSizeInBytes(size_t newBytes)
    {
        maxBytes = newBytes;
    }

    void setBufferType(BufferingType type) {
        bufferingType = type;
    }

    // Return the number of data written. Should be the same as numSamples else
    // the buffer reached the [maxBytes] meaning the buffer is full.
    size_t addData(const BufferType format, const void* data, size_t numSamples, bool *allDataAdded) {
        *allDataAdded = false;
        if (data == nullptr || numSamples == 0) {
            *allDataAdded = (numSamples == 0);
            return 0;
        }

        std::lock_guard<std::recursive_mutex> lock(bufferMutex);

        const size_t bytesNeeded = numSamples * sizeof(float);
        compactLocked(bytesNeeded);

        if (buffer.size() >= maxBytes) {
            return 0;
        }

        size_t newNumSamples = numSamples;
        if (bytesNeeded > maxBytes - buffer.size()) {
            newNumSamples = (maxBytes - buffer.size()) / sizeof(float);
            if (newNumSamples == 0) {
                return 0;
            }
        }

        const size_t oldByteSize = buffer.size();
        buffer.resize(oldByteSize + newNumSamples * sizeof(float));
        float* dst = reinterpret_cast<float*>(buffer.data() + oldByteSize);

        switch (format)
        {
            case BufferType::AUTO:
            case BufferType::PCM_F32LE:
            case BufferType::PCM_F32LE_PLANAR:
            {
                std::memcpy(dst, data, newNumSamples * sizeof(float));
                break;
            }
            case BufferType::PCM_S8:
            {
                SoLoud::convertS8ToFloats(
                    reinterpret_cast<const int8_t*>(data), dst,
                    static_cast<unsigned int>(newNumSamples));
                break;
            }
            case BufferType::PCM_S16LE:
            {
                if ((reinterpret_cast<uintptr_t>(data) & 1u) == 0) {
                    SoLoud::convertInt16ToFloats(
                        reinterpret_cast<const int16_t*>(data), dst,
                        static_cast<unsigned int>(newNumSamples));
                } else {
                    const uint8_t* bytes = static_cast<const uint8_t*>(data);
                    for (size_t i = 0; i < newNumSamples; ++i) {
                        int16_t s = 0;
                        std::memcpy(&s, bytes + i * 2, sizeof(int16_t));
                        dst[i] = static_cast<float>(s) * (1.0f / 32768.0f);
                    }
                }
                break;
            }
            case BufferType::PCM_S32LE:
            {
                if ((reinterpret_cast<uintptr_t>(data) & 3u) == 0) {
                    SoLoud::convertInt32ToFloats(
                        reinterpret_cast<const int32_t*>(data), dst,
                        static_cast<unsigned int>(newNumSamples));
                } else {
                    const uint8_t* bytes = static_cast<const uint8_t*>(data);
                    for (size_t i = 0; i < newNumSamples; ++i) {
                        int32_t s = 0;
                        std::memcpy(&s, bytes + i * 4, sizeof(int32_t));
                        dst[i] = static_cast<float>(s) * (1.0f / 2147483648.0f);
                    }
                }
                break;
            }
            default:
                buffer.resize(oldByteSize);
                return 0;
        }

        *allDataAdded = (newNumSamples == numSamples);
        return newNumSamples;
    }

    // Overload for float data, directly adding its bytes to the buffer.
    // Return the number of floats written.
    size_t addData(const float* data, size_t numSamples, bool *allDataAdded) {
        return addData(BufferType::PCM_F32LE, data, numSamples, allDataAdded);
    }

    // Remove data from the start of the buffer
    size_t removeData(size_t bytesToRemove) {
        std::lock_guard<std::recursive_mutex> lock(bufferMutex); // Lock during modification
        size_t samplesRemoved = 0;
        if (bufferingType == BufferingType::RELEASED && bytesToRemove > 0) {
            samplesRemoved = bytesToRemove / sizeof(float);
            size_t activeSize = buffer.size() > readOffset ? buffer.size() - readOffset : 0;
            if (bytesToRemove >= activeSize) {
                buffer.clear();
                readOffset = 0;
            } else {
                readOffset += bytesToRemove;
            }
        }
        return samplesRemoved;
    }

    // Function to get the current size of the buffer in floats
    size_t getFloatsBufferSize()
    {
        std::lock_guard<std::recursive_mutex> lock(bufferMutex); // Lock during read
        return getActiveSizeInBytes() / sizeof(float);
    }

    // Clear the buffer
    void clear()
    {
        std::lock_guard<std::recursive_mutex> lock(bufferMutex); // Lock during modification
        buffer.clear();
        readOffset = 0;
    }
};

#endif // BUFFER_H

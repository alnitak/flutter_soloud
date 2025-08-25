#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <mutex>

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
    std::mutex bufferMutex; // Add mutex for thread safety

public:
    // Constructor that accepts the maxBytes parameter
    Buffer() : bufferingType(BufferingType::PRESERVED), maxBytes(0) {}

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
        switch (format)
        {
            case BufferType::AUTO:
            case BufferType::OPUS:
            case BufferType::PCM_F32LE:
            {
                return addData(reinterpret_cast<const float*>(data), numSamples, allDataAdded);
            }
            break;
            case BufferType::PCM_S8:
            {
                const int8_t* data8 = reinterpret_cast<const int8_t*>(data);
                float* d = new float[numSamples];
                for (size_t i = 0; i < numSamples; ++i) {
                    d[i] = data8[i] / 128.0f;
                }
                size_t ret = addData(d, numSamples, allDataAdded);
                delete[] d;
                return ret;
            }
            break;
            case BufferType::PCM_S16LE:
            {
                const int16_t* data16 = reinterpret_cast<const int16_t*>(data);
                float* d = new float[numSamples];
                for (size_t i = 0; i < numSamples; ++i) {
                    d[i] = data16[i] / 32768.0f;
                }
                size_t ret = addData(d, numSamples, allDataAdded);
                delete[] d;
                return ret;
            }
            break;
            case BufferType::PCM_S32LE:
            {
                const int32_t* data32 = reinterpret_cast<const int32_t*>(data);
                float* d = new float[numSamples];
                for (size_t i = 0; i < numSamples; ++i) {
                    d[i] = data32[i] / 2147483648.0f;
                }
                size_t ret = addData(d, numSamples, allDataAdded);
                delete[] d;
                return ret;
            }
            break;
        }
        return 0;
    }

    // Overload for float data, directly adding its bytes to the buffer.
    // Return the number of floats written.
    size_t addData(const float* data, size_t numSamples, bool *allDataAdded) {
        std::lock_guard<std::mutex> lock(bufferMutex); // Lock during modification
        uint64_t bytesNeeded = numSamples * sizeof(float);
        int64_t newNumSamples = numSamples;
        if (buffer.size() + bytesNeeded > maxBytes)
        {
            uint64_t bytesLeft = maxBytes - buffer.size();
            newNumSamples = bytesLeft / sizeof(float);
            if (bytesLeft <= 0)
                return 0;
        }
        const int8_t* data8 = reinterpret_cast<const int8_t*>(data);  // Convert float array to int8_t array
        buffer.insert(buffer.end(), data8, data8 + newNumSamples*sizeof(float)); // Append directly
        *allDataAdded = newNumSamples == numSamples;
        return newNumSamples;
    }

    // Remove data from the start of the buffer
    size_t removeData(size_t bytesToRemove) {
        std::lock_guard<std::mutex> lock(bufferMutex); // Lock during modification
        size_t samplesRemoved = 0;
        if (bufferingType == BufferingType::RELEASED && bytesToRemove > 0) {
            samplesRemoved = bytesToRemove / sizeof(float);
            if (bytesToRemove >= buffer.size()) {
                buffer.clear();
            } else {
                buffer.erase(buffer.begin(), buffer.begin() + bytesToRemove);
            }
        }
        return samplesRemoved;
    }

    // Function to get the current size of the buffer in bytes
    size_t getFloatsBufferSize()
    {
        std::lock_guard<std::mutex> lock(bufferMutex); // Lock during read
        return buffer.size() / sizeof(float);
    }

    // Clear the buffer
    void clear()
    {
        std::lock_guard<std::mutex> lock(bufferMutex); // Lock during modification
        buffer.clear();
    }
};

#endif // BUFFER_H

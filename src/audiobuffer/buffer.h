#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

// #ifndef _IS_WIN_
// #include <algorithm>
// #endif

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>

class Buffer
{
public:
    std::vector<float> buffer; // Buffer that stores the float data

private:
    size_t maxBytes;           // Maximum capacity in bytes

    // Helper function to calculate the size of the buffer in bytes
    size_t bufferSizeInBytes() const
    {
        return buffer.size() * sizeof(float);
    }

    // Helper function to ensure the buffer does not exceed maxBytes
    void ensureCapacity(size_t newBytes)
    {
        size_t currentBytes = bufferSizeInBytes();
        if (currentBytes + newBytes > maxBytes)
        {
            // Calculate how many bytes need to be removed
            size_t bytesToRemove = currentBytes + newBytes - maxBytes;
            size_t samplesToRemove = bytesToRemove / sizeof(float);

            // Remove the necessary amount of samples from the front of the buffer
            if (samplesToRemove > 0)
            {
                buffer.erase(buffer.begin(), buffer.begin() + samplesToRemove);
            }
        }
    }

    // Template function to convert any audio format to float and append to buffer
    template <typename T>
    void appendData(const T *data, size_t numSamples, float normalizationFactor)
    {
        ensureCapacity(numSamples * sizeof(float)); // Ensure space in buffer

        for (size_t i = 0; i < numSamples; ++i)
        {
            // Normalize the value and push it to the buffer
            buffer.push_back(static_cast<float>(data[i]) / normalizationFactor);
        }
    }

public:
    Buffer() : maxBytes(0) {}

    ~Buffer()
    {
        clear();
    }

    void setSizeInBytes(size_t newBytes)
    {
        maxBytes = newBytes;
    }

    // Overload for int8_t data
    void addData(const int8_t *data, size_t numSamples)
    {
        constexpr float normalizationFactor = 128.0f; // Max value for int8_t
        appendData(data, numSamples, normalizationFactor);
    }

    // Overload for int16_t data
    void addData(const int16_t *data, size_t numSamples)
    {
        constexpr float normalizationFactor = 32768.0f; // Max value for int16_t
        appendData(data, numSamples, normalizationFactor);
    }

    // Overload for int32_t data
    void addData(const int32_t *data, size_t numSamples)
    {
        constexpr float normalizationFactor = 2147483648.0f; // Max value for int32_t
        appendData(data, numSamples, normalizationFactor);
    }

    // Overload for float data
    void addData(const float *data, size_t numSamples)
    {
        // No normalization needed for float, it's already between -1 and 1
        ensureCapacity(numSamples * sizeof(float));           // Ensure space in buffer
        buffer.insert(buffer.end(), data, data + numSamples); // Append directly
    }

    // Function to print the buffer content (for debugging)
    void printBuffer() const
    {
        for (size_t i = 0; i < buffer.size(); ++i)
        {
            std::cout << buffer[i] << " ";
        }
        std::cout << std::endl;
    }

    // Function to get the current size of the buffer in bytes
    size_t getCurrentBufferSizeInBytes() const
    {
        return bufferSizeInBytes();
    }

    // Function to get the current size of the buffer in bytes
    size_t getCurrentBufferSize() const
    {
        return buffer.size();
    }

    // Clear the buffer
    void clear()
    {
        buffer.clear();
    }
};

#endif // CIRCULAR_BUFFER_H

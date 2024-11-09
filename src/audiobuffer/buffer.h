#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

// #ifndef _IS_WIN_
// #include <algorithm>
// #endif

#include <vector>
#include <cstdint>
#include <algorithm>
#include <iostream>
#include <cstring>

enum Endianness // TODO?
{
    BUFFER_LITTLE_ENDIAN,
    BUFFER_BIG_ENDIAN
};

class Buffer
{
public:
    std::vector<int8_t> buffer; // Buffer that stores int8_t data

private:
    size_t maxBytes; // Maximum capacity in bytes

public:
    // Constructor that accepts the maxBytes parameter
    Buffer() : maxBytes(0) {}

    ~Buffer()
    {
        clear();
    }

    void setSizeInBytes(size_t newBytes)
    {
        maxBytes = newBytes;
    }

    // Overload for int8_t data, converting it to normalized float and adding its bytes to the buffer
    // Return the number of floats written.
    size_t addData(const int8_t* data, size_t numSamples) {
        float* d = new float[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 128.0f;
        }
        size_t ret = addData(d, numSamples);
        delete[] d;
        return ret;
    }

    // Overload for int16_t data, converting it to normalized float and adding its bytes to the buffer
    // Return the number of floats written.
    size_t addData(const int16_t* data, size_t numSamples) {
        float* d = new float[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 32768.0f;
        }
        size_t ret = addData(d, numSamples);
        delete[] d;
        return ret;
    }

    // Overload for int32_t data, converting it to normalized float and adding its bytes to the buffer
    // Return the number of floats written.
    size_t addData(const int32_t* data, size_t numSamples) {
        float* d = new float[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 2147483648.0f;
        }
        size_t ret = addData(d, numSamples);
        delete[] d;
        return ret;
    }

    // Overload for float data, directly adding its bytes to the buffer
    // Return the number of floats written.
    size_t addData(const float* data, size_t numSamples) {
        // No normalization needed for floats: the player is set to use f32
        unsigned int bytesNeeded = numSamples * sizeof(float);
        int32_t newNumSamples = numSamples;
        if (buffer.size() + bytesNeeded > maxBytes)
        {
            int bytesLeft = maxBytes - buffer.size();
            newNumSamples = bytesLeft / sizeof(float);
            if (bytesLeft <= 0)
                return 0;
        }
        const int8_t* data8 = reinterpret_cast<const int8_t*>(data);  // Convert float array to int8_t array
        buffer.insert(buffer.end(), data8, data8 + newNumSamples*sizeof(float)); // Append directly
        return newNumSamples;
    }

    // Function to get the current size of the buffer in bytes
    size_t getFloatsBufferSize() const
    {
        return buffer.size() / sizeof(float);
    }

    // Clear the buffer
    void clear()
    {
        buffer.clear();
    }
};

#endif // CIRCULAR_BUFFER_H

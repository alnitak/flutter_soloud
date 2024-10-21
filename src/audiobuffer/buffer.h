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

enum Endianness
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

    // Helper function to calculate the size of the buffer in bytes
    size_t bufferSizeInBytes() const
    {
        return buffer.size(); // Since each element is int8_t, the size is in bytes
    }

    // Helper function to ensure the buffer does not exceed maxBytes
    void ensureCapacity(size_t newBytes) {
        size_t currentBytes = bufferSizeInBytes();
        if (currentBytes + newBytes > maxBytes) {
            // Calculate how many bytes need to be removed
            size_t bytesToRemove = currentBytes + newBytes - maxBytes;
            
            // Remove the necessary amount of bytes from the front of the buffer
            if (bytesToRemove > 0) {
                buffer.erase(buffer.begin(), buffer.begin() + bytesToRemove);
            }
        }
    }

    // Function to convert a float (normalized between 0 and 1) and add its bytes to the buffer
    void appendFloatAsBytes(float value) {
        ensureCapacity(sizeof(float));  // Ensure space in buffer for 4 bytes
        int8_t bytes[sizeof(float)];
        std::memcpy(bytes, &value, sizeof(float));  // Copy the float into byte array
        buffer.insert(buffer.end(), bytes, bytes + sizeof(float));  // Insert all bytes
    }

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
    void addData(const int8_t* data, size_t numSamples) {
        float d[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 128.0f;
        }
        addData(d, numSamples);
    }

    // Overload for int16_t data, converting it to normalized float and adding its bytes to the buffer
    void addData(const int16_t* data, size_t numSamples) {
        float d[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 32768.0f;
        }
        addData(d, numSamples);
    }

    // Overload for int32_t data, converting it to normalized float and adding its bytes to the buffer
    void addData(const int32_t* data, size_t numSamples) {
        float d[numSamples];
        for (size_t i = 0; i < numSamples; ++i) {
            d[i] = data[i] / 2147483648.0f;
        }
        addData(d, numSamples);
    }

    // Overload for float data, directly adding its bytes to the buffer
    void addData(const float* data, size_t numSamples) {
        // No normalization needed for floats
        ensureCapacity(numSamples * sizeof(float));           // Ensure space in buffer
        const int8_t* data8 = reinterpret_cast<const int8_t*>(data);  // Convert float array to int8_t array
        buffer.insert(buffer.end(), data8, data8 + numSamples*sizeof(float)); // Append directly
    }

    // Overload for char data, directly adding its bytes to the buffer
    void addData(const unsigned char* data, size_t numSamples) {
        // No normalization needed for char
        ensureCapacity(numSamples * sizeof(float));           // Ensure space in buffer
        const unsigned char* data8 = reinterpret_cast<const unsigned char*>(data);  // Convert float array to int8_t array
        buffer.insert(buffer.end(), data8, data8 + numSamples*sizeof(unsigned char)); // Append directly
    }

    // Function to print the buffer content (for debugging)
    void printBuffer(int numSamples) const
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            std::cout /* << (int)buffer[i] << ": "*/ << (char)buffer[i];
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
        return bufferSizeInBytes() / sizeof(float);
    }

    // Clear the buffer
    void clear()
    {
        buffer.clear();
    }
};

#endif // CIRCULAR_BUFFER_H

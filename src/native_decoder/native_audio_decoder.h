#ifndef NATIVE_AUDIO_DECODER_H
#define NATIVE_AUDIO_DECODER_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

struct DecodedAudioData {
    float *samples = nullptr;        // Planar float PCM data (channel 0, channel 1, ...)
    size_t sampleCount = 0;          // Number of frames (samples per channel)
    unsigned int channels = 0;       // Number of audio channels
    float sampleRate = 0.0f;         // Sample rate in Hz
    bool success = false;
    std::string errorMessage;

    void freeSamples() {
        if (samples) {
            delete[] samples;
            samples = nullptr;
        }
        sampleCount = 0;
        channels = 0;
        sampleRate = 0.0f;
        success = false;
    }
};

class NativeAudioDecoder {
public:
    /// Check if the header matches an MP4/M4A container (starts with an 'ftyp' box).
    static inline bool isM4aOrMp4(const unsigned char *header, size_t headerSize) {
        if (header == nullptr || headerSize < 8) return false;
        // Check for 'ftyp' box at offset 4
        if (header[4] == 'f' && header[5] == 't' && header[6] == 'y' && header[7] == 'p') {
            return true;
        }
        return false;
    }

    /// Check if the header matches an ADTS AAC stream (syncword 0xFFF).
    static inline bool isAacAdts(const unsigned char *header, size_t headerSize) {
        if (header == nullptr || headerSize < 2) return false;
        return (header[0] == 0xFF && (header[1] & 0xF6) == 0xF0);
    }

    /// Check if the header matches an AC-3 or E-AC-3 stream (syncword 0x0B77).
    static inline bool isAc3OrEac3(const unsigned char *header, size_t headerSize) {
        if (header == nullptr || headerSize < 2) return false;
        return (header[0] == 0x0B && header[1] == 0x77) ||
               (header[0] == 0x77 && header[1] == 0x0B);
    }

    /// Returns true if the buffer header matches any native format supported by this decoder.
    static inline bool isSupportedNativeFormat(const unsigned char *header, size_t headerSize) {
        return isM4aOrMp4(header, headerSize) ||
               isAacAdts(header, headerSize) ||
               isAc3OrEac3(header, headerSize);
    }

    /// Decode audio from an in-memory buffer into planar 32-bit float PCM.
    static DecodedAudioData decodeMemory(const unsigned char *bytes, size_t length);

    /// Decode audio from a file path into planar 32-bit float PCM.
    static DecodedAudioData decodeFile(const char *filePath);

    /// Utility to convert interleaved float PCM [L0, R0, L1, R1, ...] into
    /// SoLoud's expected planar format [L0, L1, ..., R0, R1, ...].
    static inline void interleavedToPlanar(const float *interleaved, float *planar,
                                          size_t frameCount, unsigned int channels) {
        if (!interleaved || !planar || channels == 0 || frameCount == 0) return;
        if (channels == 1) {
            std::memcpy(planar, interleaved, frameCount * sizeof(float));
            return;
        }
        for (size_t i = 0; i < frameCount; i++) {
            for (unsigned int c = 0; c < channels; c++) {
                planar[c * frameCount + i] = interleaved[i * channels + c];
            }
        }
    }
};

#endif // NATIVE_AUDIO_DECODER_H

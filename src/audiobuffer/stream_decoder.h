#ifndef STREAM_DECODER_H
#define STREAM_DECODER_H

#include <vector>
#include <memory>
#include <exception>
#include <string>
#include <iostream>

typedef enum
{
    BUFFER_UNKNOWN,
    BUFFER_NO_ENOUGH_DATA,
    BUFFER_OGG_OPUS,
    BUFFER_OGG_VORBIS,
    BUFFER_MP3
} DetectedType;

enum class DecoderError {
    NoError,
    FormatNotSupported,
    NoOpusOggLibs,
    FailedToCreateDecoder,
    ErrorReadingPage
};

class IDecoderWrapper {
public:
    virtual ~IDecoderWrapper() = default;
    virtual bool initializeDecoder(int engineSamplerate, int engineChannels) = 0;
    virtual std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels) = 0;
};

class StreamDecoder
{
public:
    StreamDecoder()
        : mWrapper(nullptr),
          isFormatDetected(false),
          detectedType(BUFFER_UNKNOWN)
    {}

    ~StreamDecoder() = default;

    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char> &buffer, int *sampleRate, int *channels);

private:
    DetectedType detectAudioFormat(const std::vector<unsigned char> &buffer);

    /// Wrapper to the detected decoder.
    std::unique_ptr<IDecoderWrapper> mWrapper;
    bool isFormatDetected;
    DetectedType detectedType;

};

#endif // STREAM_DECODER_H

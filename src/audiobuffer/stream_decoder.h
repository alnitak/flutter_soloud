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

class DecoderException : public std::exception
{
    std::string msg;

public:
    explicit DecoderException(const std::string &m) : msg(m) {
        std::cerr << msg << std::endl;
    }
    const char *what() const noexcept override { return msg.c_str(); }
};

class FormatNotSupportedDecoder : public DecoderException
{
public:
    FormatNotSupportedDecoder()
        : DecoderException("Audio format is not supported.")
    {
    }
};

class NoOpusOggVorbisDecoder : public DecoderException
{
public:
    NoOpusOggVorbisDecoder()
        : DecoderException("NO_OPUS_OGG_LIBS environment variable is defined. Ogg/Opus/Vorbis decoding is not available."
                           " Ref: https://docs.page/alnitak/flutter_soloud_docs/get_started/no_opus_ogg_libs")
    {
    }
};

class FailedToCreateOpusOggDecoder : public DecoderException
{
public:
    FailedToCreateOpusOggDecoder(const std::string &msg)
        : DecoderException(msg) {}
};

class ErrorReadingOggPage : public DecoderException
{
public:
    ErrorReadingOggPage(const std::string &msg)
        : DecoderException(msg) {}
};

class IDecoderWrapper {
public:
    virtual ~IDecoderWrapper() = default;
    virtual bool initializeDecoder(int engineSamplerate, int engineChannels) = 0;
    virtual std::vector<float> decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels) = 0;
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

    std::vector<float> decode(std::vector<unsigned char> &buffer, int *sampleRate, int *channels);

private:
    DetectedType detectAudioFormat(const std::vector<unsigned char> &buffer);

    /// Wrapper to the detected decoder.
    std::unique_ptr<IDecoderWrapper> mWrapper;
    bool isFormatDetected;
    DetectedType detectedType;

};

#endif // STREAM_DECODER_H

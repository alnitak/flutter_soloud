#ifndef STREAM_DECODER_H
#define STREAM_DECODER_H

#include <vector>
#include <memory>
#include <exception>
#include <string>
#include <iostream>
#include <functional>
#include <map>
#include <stdint.h>

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
    ErrorReadingOggOpusPage
};

// Structure to hold track metadata
struct VorbisInfo {
    // Dont' use here the vorbis_info struct because the lib could be not linked
    int version;
    int channels;
    long rate;

  /* The below bitrate declarations are *hints*.
     Combinations of the three values carry the following implications:

     all three set to the same value:
       implies a fixed rate bitstream
     only nominal set:
       implies a VBR stream that averages the nominal bitrate.  No hard
       upper/lower limit
     upper and or lower set:
       implies a VBR bitstream that obeys the bitrate limits. nominal
       may also be set to give a nominal rate.
     none set:
       the coder does not care to speculate.
  */

  long bitrate_upper;
  long bitrate_nominal;
  long bitrate_lower;
  long bitrate_window;
};

struct OpusInfo {
    uint8_t version;
    uint8_t channels;
    uint16_t pre_skip;
    uint32_t input_sample_rate;
    int16_t output_gain;        // signed, Q8 dB
    uint8_t mapping_family;
    // Optional fields if mapping_family != 0
    uint8_t stream_count = 0;
    uint8_t coupled_count = 0;
    std::vector<uint8_t> channel_mapping;
};

struct OggOpusVorbisMetadata {
    std::string vendor;
    int commentsCount = 0;
    std::map<std::string, std::string> comments;
    VorbisInfo vorbisInfo;
    OpusInfo opusInfo;
};

struct AudioMetadata
{
    DetectedType type;
    OggOpusVorbisMetadata oggMetadata;

public:
    void debug() {
        std::string format = "Unknown";
        switch (type) {
            case BUFFER_OGG_OPUS:
                format = "Ogg Opus";
                break;
            case BUFFER_OGG_VORBIS:
                format = "Ogg Vorbis";
                break;
            case BUFFER_MP3:
                format = "MP3";
                break;
            default:
                break;
        }
        std::cout << "|-------------" << format <<  "--------------------|" << std::endl;
        std::cout << "Format: " << format << std::endl;
        if (type == BUFFER_OGG_OPUS || type == BUFFER_OGG_VORBIS) {
            std::cout << "Vendor: " << oggMetadata.vendor << std::endl;
            std::cout << "Comments: " << oggMetadata.commentsCount << std::endl;
            for (const auto& tag : oggMetadata.comments) {
                std::cout << "\t" << tag.first << ": " << tag.second << std::endl;
            }
            // Specific Vorbis information
            if (type == BUFFER_OGG_VORBIS) {
                std::cout << "Vorbis info" << std::endl;
                std::cout << "\t" << "Vorbis Version: " << oggMetadata.vorbisInfo.version << std::endl;
                std::cout << "\t" << "Channels: " << oggMetadata.vorbisInfo.channels << std::endl;
                std::cout << "\t" << "Rate: " << oggMetadata.vorbisInfo.rate << std::endl;
                std::cout << "\t" << "Bitrate Upper: " << oggMetadata.vorbisInfo.bitrate_upper << std::endl;
                std::cout << "\t" << "Bitrate Nominal: " << oggMetadata.vorbisInfo.bitrate_nominal << std::endl;
                std::cout << "\t" << "Bitrate Lower: " << oggMetadata.vorbisInfo.bitrate_lower << std::endl;
                std::cout << "\t" << "Bitrate Window: " << oggMetadata.vorbisInfo.bitrate_window << std::endl;
            }
            // Specific Opus information
            if (type == BUFFER_OGG_OPUS) {
                std::cout << "Opus info" << std::endl;
                std::cout << "\t" << "Opus Version: " << oggMetadata.opusInfo.version << std::endl;
                std::cout << "\t" << "Channels: " << oggMetadata.opusInfo.channels << std::endl;
                std::cout << "\t" << "Pre Skip: " << oggMetadata.opusInfo.pre_skip << std::endl;
                std::cout << "\t" << "Input Sample Rate: " << oggMetadata.opusInfo.input_sample_rate << std::endl;
                std::cout << "\t" << "Output Gain: " << oggMetadata.opusInfo.output_gain << std::endl;
                std::cout << "\t" << "Mapping Family: " << oggMetadata.opusInfo.mapping_family << std::endl;
                std::cout << "\t" << "Stream Count: " << oggMetadata.opusInfo.stream_count << std::endl;
                std::cout << "\t" << "Coupled Count: " << oggMetadata.opusInfo.coupled_count << std::endl;
                for (int i = 0; i < oggMetadata.opusInfo.channel_mapping.size(); i++) {
                    std::cout << "\t" << "Channel Mapping[" << i << "]: " << oggMetadata.opusInfo.channel_mapping[i] << std::endl;
                }
            }
        }
        std::cout << "|---------------------------------|" << std::endl;
    }
};

// Callback type for track change notifications
using TrackChangeCallback = std::function<void(const AudioMetadata&)>;

class IDecoderWrapper {
public:
    virtual ~IDecoderWrapper() = default;
    virtual bool initializeDecoder(int engineSamplerate, int engineChannels) = 0;
    virtual std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char>& buffer, int* sampleRate, int* channels) = 0;
    
    void setTrackChangeCallback(TrackChangeCallback callback) {
        onTrackChange = callback;
    }

protected:
    TrackChangeCallback onTrackChange;
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

    std::pair<std::vector<float>, DecoderError> decode(std::vector<unsigned char> &buffer,
        int *sampleRate,
        int *channels,
        TrackChangeCallback metadataChangeCallback);

private:
    DetectedType detectAudioFormat(const std::vector<unsigned char> &buffer);

    /// Wrapper to the detected decoder.
    std::unique_ptr<IDecoderWrapper> mWrapper;
    bool isFormatDetected;
    DetectedType detectedType;

};

#endif // STREAM_DECODER_H

#ifndef STREAM_DECODER_H
#define STREAM_DECODER_H

#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdint.h>
#include <string>
#include <vector>

typedef enum {
  BUFFER_UNKNOWN,
  BUFFER_NO_ENOUGH_DATA,
  BUFFER_OGG_OPUS,
  BUFFER_OGG_VORBIS,
  BUFFER_OGG_FLAC,
  BUFFER_FLAC,
  BUFFER_MP3_WITH_ID3,
  BUFFER_MP3_STREAM,
  BUFFER_WAV
} DetectedType;

enum class DecoderError {
  NoError,
  FormatNotSupported,
  NoXiphLibs,
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
  int16_t output_gain; // signed, Q8 dB
  uint8_t mapping_family;
  // Optional fields if mapping_family != 0
  uint8_t stream_count = 0;
  uint8_t coupled_count = 0;
  std::vector<uint8_t> channel_mapping;
};

struct FlacInfo {
  uint32_t min_blocksize, max_blocksize;
  uint32_t min_framesize, max_framesize;
  uint32_t sample_rate;
  uint32_t channels;
  uint32_t bits_per_sample;
  uint32_t total_samples;
};

struct OggMetadata {
  std::string vendor;
  int commentsCount = 0;
  std::map<std::string, std::string> comments;
  FlacInfo flacInfo;
  VorbisInfo vorbisInfo;
  OpusInfo opusInfo;
};

struct Mp3Metadata {
  std::string title;
  std::string artist;
  std::string album;
  std::string date;
  std::string genre;
};

struct AudioMetadata {
  DetectedType type;
  Mp3Metadata mp3Metadata;
  OggMetadata oggMetadata;

public:
  void debug() {
    std::string format = "Unknown";
    switch (type) {
    case DetectedType::BUFFER_OGG_OPUS:
      format = "Ogg Opus";
      break;
    case DetectedType::BUFFER_OGG_VORBIS:
      format = "Ogg Vorbis";
      break;
    case DetectedType::BUFFER_OGG_FLAC:
      format = "Ogg FLAC";
      break;
    case DetectedType::BUFFER_MP3_STREAM:
      format = "MP3 stream";
      break;
    case DetectedType::BUFFER_MP3_WITH_ID3:
      format = "MP3 with ID3";
      break;
    case DetectedType::BUFFER_WAV:
      format = "WAV";
      break;
    default:
      break;
    }
    std::cout << "|-------------" << format << "--------------------|"
              << std::endl;
    std::cout << "Format: " << format << std::endl;
    if (type == DetectedType::BUFFER_MP3_WITH_ID3 ||
        type == DetectedType::BUFFER_MP3_STREAM) {
      std::cout << "Title: " << mp3Metadata.title << std::endl;
      std::cout << "Artist: " << mp3Metadata.artist << std::endl;
      std::cout << "Album: " << mp3Metadata.album << std::endl;
      std::cout << "Date: " << mp3Metadata.date << std::endl;
      std::cout << "Genre: " << mp3Metadata.genre << std::endl;
    } else if (type == DetectedType::BUFFER_OGG_OPUS ||
               type == DetectedType::BUFFER_OGG_VORBIS ||
               type == DetectedType::BUFFER_OGG_FLAC) {
      std::cout << "Vendor: " << oggMetadata.vendor << std::endl;
      std::cout << "Comments: " << oggMetadata.commentsCount << std::endl;
      for (const auto &tag : oggMetadata.comments) {
        std::cout << "\t" << tag.first << ": " << tag.second << std::endl;
      }
      // Specific Vorbis information
      if (type == DetectedType::BUFFER_OGG_VORBIS) {
        std::cout << "Vorbis info" << std::endl;
        std::cout << "\t"
                  << "Vorbis Version: " << oggMetadata.vorbisInfo.version
                  << std::endl;
        std::cout << "\t" << "Channels: " << oggMetadata.vorbisInfo.channels
                  << std::endl;
        std::cout << "\t" << "Rate: " << oggMetadata.vorbisInfo.rate
                  << std::endl;
        std::cout << "\t"
                  << "Bitrate Upper: " << oggMetadata.vorbisInfo.bitrate_upper
                  << std::endl;
        std::cout << "\t" << "Bitrate Nominal: "
                  << oggMetadata.vorbisInfo.bitrate_nominal << std::endl;
        std::cout << "\t"
                  << "Bitrate Lower: " << oggMetadata.vorbisInfo.bitrate_lower
                  << std::endl;
        std::cout << "\t"
                  << "Bitrate Window: " << oggMetadata.vorbisInfo.bitrate_window
                  << std::endl;
      }
      // Specific Opus information
      if (type == DetectedType::BUFFER_OGG_OPUS) {
        std::cout << "Opus info" << std::endl;
        std::cout << "\t" << "Opus Version: "
                  << static_cast<int16_t>(oggMetadata.opusInfo.version)
                  << std::endl;
        std::cout << "\t" << "Channels: "
                  << static_cast<int16_t>(oggMetadata.opusInfo.channels)
                  << std::endl;
        std::cout << "\t" << "Pre Skip: " << oggMetadata.opusInfo.pre_skip
                  << std::endl;
        std::cout << "\t" << "Input Sample Rate: "
                  << oggMetadata.opusInfo.input_sample_rate << std::endl;
        std::cout << "\t" << "Output Gain: " << oggMetadata.opusInfo.output_gain
                  << std::endl;
        std::cout << "\t" << "Mapping Family: "
                  << static_cast<int16_t>(oggMetadata.opusInfo.mapping_family)
                  << std::endl;
        std::cout << "\t" << "Stream Count: "
                  << static_cast<int16_t>(oggMetadata.opusInfo.stream_count)
                  << std::endl;
        std::cout << "\t" << "Coupled Count: "
                  << static_cast<int16_t>(oggMetadata.opusInfo.coupled_count)
                  << std::endl;
        for (int i = 0; i < oggMetadata.opusInfo.channel_mapping.size(); i++) {
          std::cout << "\t" << "Channel Mapping[" << i
                    << "]: " << oggMetadata.opusInfo.channel_mapping[i]
                    << std::endl;
        }
      }
      // Specific FLAC information
      if (type == DetectedType::BUFFER_OGG_FLAC) {
        std::cout << "FLAC info" << std::endl;
        std::cout << "\t"
                  << "Min Block Size: " << oggMetadata.flacInfo.min_blocksize
                  << std::endl;
        std::cout << "\t"
                  << "Max Block Size: " << oggMetadata.flacInfo.max_blocksize
                  << std::endl;
        std::cout << "\t"
                  << "Min Frame Size: " << oggMetadata.flacInfo.min_framesize
                  << std::endl;
        std::cout << "\t"
                  << "Max Frame Size: " << oggMetadata.flacInfo.max_framesize
                  << std::endl;
        std::cout << "\t" << "Sample Rate: " << oggMetadata.flacInfo.sample_rate
                  << std::endl;
        std::cout << "\t" << "Channels: " << oggMetadata.flacInfo.channels
                  << std::endl;
        std::cout << "\t"
                  << "Bits Per Sample: " << oggMetadata.flacInfo.bits_per_sample
                  << std::endl;
        std::cout << "\t"
                  << "Total Samples: " << oggMetadata.flacInfo.total_samples
                  << std::endl;
      }
    }
    std::cout << "|---------------------------------|" << std::endl;
  }
};

// Callback type for track change notifications
using TrackChangeCallback = std::function<void(const AudioMetadata &)>;

class IDecoderWrapper {
public:
  virtual ~IDecoderWrapper() = default;
  virtual bool initializeDecoder(int engineSamplerate, int engineChannels) = 0;
  virtual std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *sampleRate,
         int *channels, size_t maxOutputSamples = 0) = 0;

  // Called when no more data will be added to signal end-of-stream
  virtual void setDataEnded() {}

  /// Return true if this decoder can currently map a target time to a byte
  /// offset in the encoded stream. For compressed formats this may become
  /// true only after enough data/metadata has been seen.
  virtual bool canSeekToTime(double seconds) const {
    (void)seconds;
    return false;
  }

  /// Return the encoded byte offset that corresponds to the given time, or 0
  /// if the mapping is not yet known.
  virtual uint64_t timeToByteOffset(double seconds) {
    (void)seconds;
    return 0;
  }

  /// Return the total stream duration in seconds, or a negative value if
  /// the duration is not yet known.
  virtual double getDuration() const { return -1.0; }

  /// Set the total encoded audio size in bytes. Wrappers may use this to
  /// estimate duration for formats that do not store it in the header.
  virtual void setTotalAudioSizeBytes(uint64_t size) { (void)size; }

  /// Return the number of samples to skip at the start of the stream.
  /// Relevant for Ogg Opus (header pre_skip); zero for other formats.
  virtual int preSkip() const { return 0; }

  /// Return the sample rate used by the Ogg granule position. For Opus this
  /// is always 48000 Hz; for Vorbis it is the stream sample rate.
  virtual int granuleSampleRate() const { return 0; }

  /// Prepare the decoder for an out-of-buffer seek. The wrapper should keep
  /// any header/initialization state that is still valid and reset only the
  /// parts that depend on the current stream position. For Ogg-based
  /// wrappers this clears the Ogg sync state while preserving the header
  /// info so that mid-stream data can be decoded after a seek.
  /// [targetSample] is the absolute decoded sample position the caller will
  /// continue from, in the wrapper's output sample rate.
  virtual void prepareForSeek(uint64_t targetSample) { (void)targetSample; }

  void setTrackChangeCallback(TrackChangeCallback callback) {
    onTrackChange = callback;
  }

  DetectedType detectedType;

protected:
  TrackChangeCallback onTrackChange;
};

class StreamDecoder {
public:
  StreamDecoder()
      : mWrapper(nullptr), isFormatDetected(false), mIcyMetaInt(0),
        mTotalAudioSizeBytes(0), mDataEndedPending(false) {}

  ~StreamDecoder() = default;

  void setBufferIcyMetaInt(int icyMetaInt);

  /// Signal that no more data will be added to the stream.
  ///
  /// Forwards immediately if the decoder wrapper exists, otherwise stores
  /// the signal and forwards it when the wrapper is created in decode().
  /// Without deferred delivery the signal is lost when total data is below
  /// the 32 KB threshold in addData(), because the wrapper has not been
  /// created yet at the time BufferStream::setDataIsEnded() calls this.
  void setDataEnded() {
    mDataEndedPending = true;
    if (mWrapper) {
      mWrapper->setDataEnded();
    }
  }

  std::pair<std::vector<float>, DecoderError>
  decode(std::vector<unsigned char> &buffer, int *sampleRate, int *channels,
         TrackChangeCallback metadataChangeCallback,
         size_t maxOutputSamples = 0);

  DetectedType getWrapperType();

  bool canSeekToTime(double seconds) const;
  uint64_t timeToByteOffset(double seconds);
  double getDuration() const;

  /// Return the Ogg header pre-skip, if any.
  int preSkip() const;

  /// Return the sample rate of the Ogg granule position.
  int granuleSampleRate() const;

  /// Prepare the wrapped decoder for a seek to [targetSample].
  void prepareForSeek(uint64_t targetSample);

  /// Set the total encoded audio size in bytes. The value is forwarded to the
  /// wrapped decoder as soon as it is created, so wrappers can estimate
  /// duration for formats that do not store it in the header.
  void setTotalAudioSizeBytes(uint64_t size) {
    mTotalAudioSizeBytes = size;
    if (mWrapper) {
      mWrapper->setTotalAudioSizeBytes(size);
    }
  }

private:
  DetectedType detectAudioFormat(const std::vector<unsigned char> &buffer);

  /// Wrapper to the detected decoder.
  std::unique_ptr<IDecoderWrapper> mWrapper;
  bool isFormatDetected;
  int mIcyMetaInt;
  uint64_t mTotalAudioSizeBytes;

  /// Deferred end-of-stream flag. Set by setDataEnded() when the wrapper
  /// does not exist yet. Forwarded to the wrapper in decode() once created.
  bool mDataEndedPending;
};

#endif // STREAM_DECODER_H

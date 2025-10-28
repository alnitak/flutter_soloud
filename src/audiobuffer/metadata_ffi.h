#pragma once

#include <stdint.h>

#define MAX_STRING_LENGTH 1024
#define MAX_COMMENTS 32
#define MAX_CHANNEL_MAPPING 8

/// Strucs to send metadata from C to Dart.
/// The "AudioMetadata" strucs got from *_stream_decoder.cpp in the getMetadata(), 
/// will be converted to these to be sent to Dart.

typedef enum
{
    UNKNOWN,
    OGG_OPUS,
    OGG_VORBIS,
    OGG_FLAC,
    MP3_WITH_ID3,
    MP3_STREAM
} DetectedTypeFFI;

// Structure to hold track metadata
struct VorbisInfoFFI {
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

// Comment key-value pair structure
struct CommentPairFFI {
    char key[MAX_STRING_LENGTH];
    char value[MAX_STRING_LENGTH];
};

struct OpusInfoFFI {
    uint8_t version;
    uint8_t channels;
    uint16_t pre_skip;
    uint32_t input_sample_rate;
    int16_t output_gain;        // signed, Q8 dB
    uint8_t mapping_family;
    // Optional fields if mapping_family != 0
    uint8_t stream_count;
    uint8_t coupled_count;
    uint8_t channel_mapping[MAX_CHANNEL_MAPPING];
    int channel_mapping_size;
};

struct FlacInfoFFI {
  uint32_t min_blocksize, max_blocksize;
	uint32_t min_framesize, max_framesize;
	uint32_t sample_rate;
	uint32_t channels;
	uint32_t bits_per_sample;
	uint32_t total_samples;
};

struct OggMetadataFFI {
    char vendor[MAX_STRING_LENGTH];
    uint32_t commentsCount;
    struct CommentPairFFI comments[MAX_COMMENTS];
    struct VorbisInfoFFI vorbisInfo;
    struct OpusInfoFFI opusInfo;
    struct FlacInfoFFI flacInfo;
};

struct Mp3MetadataFFI {
    char title[MAX_STRING_LENGTH];
    char artist[MAX_STRING_LENGTH];
    char album[MAX_STRING_LENGTH];
    char date[MAX_STRING_LENGTH];
    char genre[MAX_STRING_LENGTH];
};

struct AudioMetadataFFI
{
    DetectedTypeFFI detectedType;
    struct Mp3MetadataFFI mp3Metadata;
    struct OggMetadataFFI oggMetadata;
};

// callback to tell dart the metadata
typedef void (*dartOnMetadataCallback_t)(struct AudioMetadataFFI metadata);

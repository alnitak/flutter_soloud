#pragma once

#ifndef ENUMS_H
#define ENUMS_H

#include <stdbool.h> // for ffigen to not complain about bool type

/// Possible player errors.
///
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum PlayerErrors
{
    /// No error
    noError = 0,
    /// Some parameter is invalid
    invalidParameter = 1,
    /// File not found 
    fileNotFound = 2,
    /// File found, but could not be loaded
    fileLoadFailed = 3,
    /// The sound file has already been loaded
    fileAlreadyLoaded = 4,
    /// DLL not found, or wrong DLL
    dllNotFound = 5,
    /// Out of memory
    outOfMemory = 6,
    /// Feature not implemented
    notImplemented = 7,
    /// Other error
    unknownError = 8,
    /// Player not initialized
    nullPointer = 9,
    /// Audio sound hash is not found
    soundHashNotFound = 10,
    /// Player not initialized
    backendNotInited = 11,
    /// Filter not found
    filterNotFound = 12,
    /// Asking for wave and FFT is not enabled
    visualizationNotEnabled = 13,
    /// The maximum number of filters has been reached (default is 8).
    maxNumberOfFiltersReached = 14,
    /// The filter has already been added.
    filterAlreadyAdded = 15,
    /// Player already inited.
    playerAlreadyInited = 16,
    /// Audio handle is not found.
    soundHandleNotFound = 17,
    /// Error getting filter parameter.
    filterParameterGetError = 18,
    /// No playback devices were found.
    noPlaybackDevicesFound = 19,
    /// Trying to add PCM data but the buffer is full or not large
    /// enough for the neded PCM data. Try increasing the buffer size.
    pcmBufferFull = 20,
    /// Given hash doesn't belong to a buffer stream.
    hashIsNotABufferStream = 21,
    /// Trying to add PCM data but the stream is marked to be ended
    /// already, by the user or when the stream reached its maximum
    /// capacity, in this case the stream is automatically marked to be ended.
    streamEndedAlready = 22,
    /// Failed to create Opus decoder.
    failedToCreateOpusDecoder = 23,
    /// Failed to decode Opus packet.
    failedToDecodeOpusPacket = 24,
    /// A BufferStream using `release` buffer type can be played only once.
    bufferStreamCanBePlayedOnlyOnce = 25,
    /// The maximum number of active voices has been reached.
    maxActiveVoiceCountReached = 26
} PlayerErrors_t;

/// Possible read sample errors
typedef enum ReadSamplesErrors
{
    /// No error
    readSamplesNoError = 0,
    /// Initialization failed. Probably an unsupported format.
    noBackend,
    /// Failed to retrieve decoder data format.
    failedToGetDataFormat,
    /// Failed to seek audio data.
    failedToSeekPcm,
    /// Failed to read PCM frames.
    failedToReadPcmFrames
} ReadSamplesErrors_t;

typedef enum PlayerStateEvents
{
    event_started = 0,
    event_stopped,
    event_rerouted,
    event_interruption_began,
    event_interruption_ended,
    event_unlocked,
} PlayerEvents_t;

typedef enum SoundType
{
    // using Soloud::wav
    TYPE_WAV,
    // using Soloud::wavStream
    TYPE_WAVSTREAM,
    // this sound is a waveform
    TYPE_SYNTH,
    // this sound is a streaming buffer
    TYPE_BUFFER_STREAM,
} SoundType_t;

typedef enum FilterType
{
    BiquadResonantFilter,
    EqFilter,
    EchoFilter,
    LofiFilter,
    FlangerFilter,
    BassboostFilter,
    WaveShaperFilter,
    RobotizeFilter,
    FreeverbFilter,
    PitchShiftFilter,
    LimiterFilter,
    CompressorFilter
} FilterType_t;

/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum BufferType
{
    PCM_F32LE = 0,
    PCM_S8 = 1,
    PCM_S16LE = 2,
    PCM_S32LE = 3,
    OPUS = 4,
} BufferType_t;


typedef struct PCMformat
{
  unsigned int sampleRate;
  unsigned int channels;
  unsigned int bytesPerSample;
  BufferType dataType;
} PCMformat;


typedef void (*dartOnBufferingCallback_t)(bool isBuffering, unsigned int handle, double time);

#endif // ENUMS_H
#include <stdint.h>
#include <stdio.h>

#ifndef ENUMS_H
#define ENUMS_H

/// Possible player errors.
///
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum PlayerErrors {
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
  /// enough for the needed PCM data. Try increasing the buffer size.
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
  maxActiveVoiceCountReached = 26,
  /// Trying to get time consumed from wrong buffer type.
  wrongBufferTypeToAskForTimeConsumed = 27,
  /// Buffer stream with released buffer type, cannot be seeked.
  bufferStreamWithReleasedBufferTypeCannotBeSeeked = 28,
  /// Audio format not supported.
  audioFormatNotSupported = 29,
  /// Xiph libraries not found.
  xiphLibsNotFound = 30,
  /// Bus ID not found.
  busIdNotFound = 31,
  /// Given hash doesn't belong to a pull buffer stream.
  hashIsNotAPullBufferStream = 32,
  /// The pull buffer stream is in an invalid state for this operation.
  invalidPullBufferState = 33,
  /// The output audio device could not be started or resumed.
  audioDeviceFailedToStart = 34,
  /// SoLoud didn't return a valid voice handle when starting the playback.
  failedToStartPlayback = 35,
} PlayerErrors_t;

/// Possible read sample errors
typedef enum ReadSamplesErrors {
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

typedef enum PlayerStateEvents {
  event_started = 0,
  event_stopped,
  event_rerouted,
  event_interruption_began,
  event_interruption_ended,
  event_unlocked,
  /// An automatic output-device start failed, after the backend had already
  /// rebuilt the device and retried. Emitted by the lifecycle scheduler, not by
  /// the OS, so unlike the notifications above it is reliable on every backend.
  ///
  /// WARNING: Keep in sync with `PlayerStateNotification` in
  /// `lib/src/enums.dart`; the Dart side indexes this by ordinal.
  event_audio_device_start_failed,
} PlayerEvents_t;

/// The state of the audio output device.
///
/// The values mirror miniaudio's `ma_device_state` so they can be returned
/// directly from the backend without translation.
///
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum AudioDeviceState {
  /// The device is uninitialized. Also returned before the engine is
  /// initialized or after it has been deinitialized.
  audioDeviceUninitialized = 0,
  /// The device is stopped. This is the device's default state right after
  /// initialization.
  audioDeviceStopped = 1,
  /// The device is started and is requesting and/or delivering audio data.
  audioDeviceStarted = 2,
  /// The device is transitioning from a stopped state to a started state.
  audioDeviceStarting = 3,
  /// The device is transitioning from a started state to a stopped state.
  audioDeviceStopping = 4,
} AudioDeviceState_t;

typedef enum SoundType {
  // using Soloud::wav
  TYPE_WAV,
  // using Soloud::wavStream
  TYPE_WAVSTREAM,
  // this sound is a waveform
  TYPE_SYNTH,
  // this sound is a streaming buffer
  TYPE_BUFFER_STREAM,
  // this sound is a text to speech
  TYPE_TEXT_TO_SPEECH,
  // this sound is a pull-based streaming buffer
  TYPE_PULL_BUFFER_STREAM
} SoundType_t;

typedef enum FilterType {
  BiquadResonantFilter,
  EchoFilter,
  LofiFilter,
  FlangerFilter,
  BassboostFilter,
  WaveShaperFilter,
  RobotizeFilter,
  FreeverbFilter,
  PitchShiftFilter,
  LimiterFilter,
  CompressorFilter,
  ParametricEQFilter
} FilterType_t;

/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum BufferType {
  PCM_F32LE = 0,
  PCM_S8 = 1,
  PCM_S16LE = 2,
  PCM_S32LE = 3,
  OPUS = 4,
  AUTO = 5,
} BufferType_t;

/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum MixerOutputFormat {
  MIXER_OUTPUT_PCM_F32LE = 0,
  MIXER_OUTPUT_PCM_S8 = 1,
  MIXER_OUTPUT_PCM_S16LE = 2,
  MIXER_OUTPUT_PCM_S32LE = 3,
  MIXER_OUTPUT_OPUS = 4,
  MIXER_OUTPUT_VORBIS = 5,
  MIXER_OUTPUT_FLAC = 6,
  MIXER_OUTPUT_WAV = 7,
} MixerOutputFormat_t;

typedef struct PCMformat {
  unsigned int sampleRate;
  unsigned int channels;
  unsigned int bytesPerSample;
  enum BufferType dataType;
} PCMformat;

// callback to tell dart that we are buffering/unbuffering
typedef void (*dartOnBufferingCallback_t)(bool isBuffering, unsigned int handle,
                                          double time);

// callback to tell dart that more encoded data is needed
typedef void (*dartOnMoreDataIsNeededCallback_t)(uint64_t offset);

// callback to tell dart the total audio duration of a pull-buffer stream
typedef void (*dartOnAudioDurationCallback_t)(double duration);

// callback handing dart a chunk of captured mixer output. Declared here rather
// than in bindings.cpp so `ffi_gen_tmp.h` -- the ffigen entry point -- can name
// it in the exports that take one.
typedef void (*dartMixerOutputDataCallback_t)(unsigned char *data,
                                              uint64_t length);

/// The kind of visualization data to acquire.
///
/// WARNING: Keep these in sync with `lib/src/enums.dart`.
typedef enum VisualizationKind {
  VISUALIZATION_WAVE = 0,
  VISUALIZATION_FFT = 1,
  VISUALIZATION_WAVE_AND_FFT = 2,
} VisualizationKind_t;

// Channel selection constants for visualization:
#define VISUALIZATION_CHANNEL_MERGED -1
#define VISUALIZATION_CHANNEL_ALL -2

// Callback handing dart visualization data (wave and/or FFT) per channel.
typedef void (*dartVisualizationCallback_t)(
    int32_t channelCount,
    const float **waveDataPerChannel,
    int32_t waveSamples,
    const float **fftDataPerChannel,
    int32_t fftSamples);

#endif // ENUMS_H
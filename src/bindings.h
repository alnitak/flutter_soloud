/// Declarations of every FFI_PLUGIN_EXPORT entry point defined in
/// `src/bindings.cpp`. This header is the single source of truth for the
/// native ABI: `src/bindings.cpp` includes it so the definitions are checked
/// against these declarations, and it is the ffigen entry point used to
/// generate `lib/src/bindings/flutter_soloud_ffigen.dart`.
///
/// To regenerate the Dart bindings, go into the "flutter_soloud" dir from the
/// root project dir and run:
///
/// dart run ffigen --config ffigen.yaml
///
/// or if stdio.h/standard headers are not found on macOS:
/// CPATH="$(xcrun --show-sdk-path)/usr/include" dart run ffigen --config ffigen.yaml
///
/// or on Linux:
/// export CPATH="$(clang -v 2>&1 | grep "Selected GCC installation" | rev | cut -d' ' -f1 | rev)/include";  dart run ffigen --config ffigen.yaml
///
/// The generated code will be placed into lib/src/bindings/flutter_soloud_ffigen.dart.
///
/// Functions behind platform/test guards in bindings.cpp are behind the same
/// guards here: ffigen parses this header for the host platform, so the
/// __EMSCRIPTEN__ web-worker exports and the SOLOUD_LIFECYCLE_TEST_HOOKS
/// test-only exports are intentionally not visible to it.

#pragma once

#ifndef BINDINGS_H
#define BINDINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "enums.h"
#include "audiobuffer/metadata_ffi.h"

// Same definition as in soloud_common.h, guarded so the macro is defined once
// no matter which header is included first.
#ifndef FFI_PLUGIN_EXPORT
#if defined(_WIN32) || defined(_WIN64)
#define FFI_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FFI_PLUGIN_EXPORT                                                      \
  __attribute__((visibility("default"))) __attribute__((used))
#endif
#endif

// Callback typedefs owned by bindings.cpp (the mixer-output, visualization,
// buffering and metadata callback typedefs come from enums.h and
// audiobuffer/metadata_ffi.h).
typedef void (*dartVoiceEndedCallback_t)(unsigned int *);
typedef void (*dartFileLoadedCallback_t)(enum PlayerErrors *,
                                         char *completeFileName,
                                         unsigned int *, uint64_t *counter);
typedef void (*dartStateChangedCallback_t)(enum PlayerStateEvents *);

#ifdef __cplusplus
extern "C"
{
#endif

  //////////////////////////////////////////////////////////////
  /// WEB WORKER (web only, not visible to ffigen)

#ifdef __EMSCRIPTEN__
  /// Create the web worker and store a global "Module_soloud.workerUri" in JS.
  FFI_PLUGIN_EXPORT bool createWorkerInWasm();

  /// Post a message with the web worker.
  FFI_PLUGIN_EXPORT void sendToWorker(const char *message, int value);

  /// Returns the current engine session counter (see [engineGeneration]).
  FFI_PLUGIN_EXPORT unsigned int getEngineGeneration();
#endif

  //////////////////////////////////////////////////////////////
  /// Dart callbacks, engine lifecycle and mixer output capture

  FFI_PLUGIN_EXPORT void nativeFree(void *pointer);

  /// The callback to monitor when a voice ends.
  ///
  /// It is called by void `Soloud::stopVoice_internal(unsigned int aVoice)`
  /// when a voice ends and comes from the audio thread (so on the web, from a
  /// different web worker).
  FFI_PLUGIN_EXPORT void voiceEndedCallback(unsigned int *handle);

  /// Requests a device-idle evaluation after SoLoud stops or pauses a voice.
  /// SoLoud invokes this only after releasing its audio mutex.
  FFI_PLUGIN_EXPORT void voiceInactiveCallback();

  /// Set a Dart functions to call when an event occurs.
  ///
  /// [owner_engine_id] is the FlutterEngine whose isolate created these
  /// trampolines, or -1 where no engine lifecycle is available. It is recorded
  /// so that a detaching or hot-restarting engine can retire *its own*
  /// callables and never somebody else's.
  FFI_PLUGIN_EXPORT void
  setDartEventCallback(dartVoiceEndedCallback_t voice_ended_callback,
                       dartFileLoadedCallback_t file_loaded_callback,
                       dartStateChangedCallback_t state_changed_callback,
                       int64_t owner_engine_id);

  FFI_PLUGIN_EXPORT void clearDartCallbackRegistrations();

  /// Retire every Dart callable owned by [engine_id] because its isolate is
  /// going away (a hot restart, or its FlutterEngine being destroyed). Returns
  /// false when a different engine owns the live registration, so a detaching
  /// engine never retires another one's callables.
  FFI_PLUGIN_EXPORT bool clearDartCallbackRegistrationsForEngine(
      int64_t engine_id);

  FFI_PLUGIN_EXPORT void retireDartCallbacksFinalizer(void *token);

  /// Start capturing the master mixer output.
  ///
  /// [format] the output format from the MixerOutputFormat enum.
  /// [sampleRate] desired sample rate, or <=0 to use the engine sample rate.
  /// [channels] desired channel count, or <=0 to use the engine channel count.
  /// [bufferSizeBytes] size of the circular buffer in bytes.
  /// [notificationThresholdBytes] number of available bytes before a callback
  /// is fired.
  /// [chunkPCMFrames] fixed PCM chunk size in frames, or 0 to disable chunk
  /// mode.
  /// Returns [PlayerErrors.noError] if success.
  FFI_PLUGIN_EXPORT enum PlayerErrors startMixerCapture(
      int format, int sampleRate, int channels, int bufferSizeBytes,
      int notificationThresholdBytes, int chunkPCMFrames);

  /// Stop capturing the master mixer output.
  FFI_PLUGIN_EXPORT void stopMixerCapture();

  /// Returns 1 if mixer capture is running, 0 otherwise.
  FFI_PLUGIN_EXPORT int isMixerCaptureRunning();

  /// Get the pointer to the circular capture buffer.
  FFI_PLUGIN_EXPORT unsigned char *getMixerCaptureBufferPointer();

  /// Get the total size of the circular capture buffer in bytes.
  FFI_PLUGIN_EXPORT int getMixerCaptureBufferSize();

  /// Get the number of bytes available to read in the capture buffer.
  FFI_PLUGIN_EXPORT int getMixerCaptureAvailableBytes();

  /// Get the current read offset in the capture buffer.
  FFI_PLUGIN_EXPORT int getMixerCaptureReadOffset();

  /// Advance the read position by [bytes].
  FFI_PLUGIN_EXPORT void advanceMixerCaptureReadPosition(int bytes);

  /// Get a malloc'd copy of the WAV header of the current capture, or nullptr
  /// when the capture format has none. Free with nativeFree().
  FFI_PLUGIN_EXPORT unsigned char *getMixerOutputWavHeader();

  /// Publish the mixer-output data callable for [owner_engine_id].
  /// Returns whether the callable was published.
  FFI_PLUGIN_EXPORT bool setMixerOutputCallbackForEngine(
      dartMixerOutputDataCallback_t callback, int64_t owner_engine_id);

  /// Lifecycle-free entry point, for hosts where no registration is ever
  /// claimed. The web build binds this exported symbol directly from the
  /// prebuilt wasm, so its signature must not change.
  FFI_PLUGIN_EXPORT void setMixerOutputCallback(
      dartMixerOutputDataCallback_t callback);

  FFI_PLUGIN_EXPORT bool setVisualizationCallbackForEngine(
      dartVisualizationCallback_t callback, int64_t owner_engine_id);

  FFI_PLUGIN_EXPORT void setVisualizationCallback(
      dartVisualizationCallback_t callback);

  //////////////////////////////////////////////////////////////
  /// Engine

  /// Check if the libopus and libogg are available at build time.
  FFI_PLUGIN_EXPORT bool areXiphLibsAvailable();

  /// Initialize the player. Must be called before any other player functions.
  ///
  /// [sampleRate] the sample rate. Usually is 22050, 44100 (CD quality) or
  /// 48000.
  /// [bufferSize] the audio buffer size. Usually is 2048, but can be also 512
  /// when low latency is needed for example in games. [channels] 1=mono,
  /// 2=stereo, 4=quad, 6=5.1, 8=7.1.
  /// [devicePeriodFrames] small output device period used when
  /// [renderAheadFrames] enables the render-ahead ring; 0 = default (512).
  /// [renderAheadFrames] depth of the engine-owned render-ahead ring in
  /// frames; 0 disables it and keeps direct-to-device mixing.
  ///
  /// Returns [PlayerErrors.noError] if success.
  FFI_PLUGIN_EXPORT enum PlayerErrors initEngine(int deviceID,
                                                 unsigned int sampleRate,
                                                 unsigned int bufferSize,
                                                 unsigned int channels,
                                                 unsigned int lowLatency,
                                                 unsigned int devicePeriodFrames,
                                                 unsigned int renderAheadFrames);

  /// Android only: choose whether SoLoud tags the AAudio stream's
  /// usage/contentType (media/music) or leaves them unset so the app can
  /// manage AudioAttributes externally (e.g. via the audio_session plugin).
  /// Only takes effect with low-latency disabled. Call before initEngine().
  /// No effect on other backends. [managed] != 0 → media/music (default);
  /// 0 → leave unset.
  FFI_PLUGIN_EXPORT void setAndroidAAudioAttributes(unsigned int managed);

  /// Set how long the audio output device keeps running while the engine is
  /// idle (no active voices) before it is automatically stopped, on every
  /// platform. [timeoutMs] < 0 keeps the device running indefinitely while
  /// idle (the deferred idle-pause is suppressed, so the device keeps
  /// rendering silence and the app keeps its OS audio session alive) and
  /// starts it immediately if it was stopped. [timeoutMs] == 0 stops the
  /// device as soon as possible once idle. [timeoutMs] > 0 keeps it running
  /// for that many milliseconds after going idle. Any play/unpause before the
  /// deadline cancels the pending stop. The default is 500. Can be called any
  /// time.
  FFI_PLUGIN_EXPORT void setAudioDeviceIdleTimeout(int64_t timeoutMs);

  /// Stop the audio output device without deinitializing the engine. By
  /// default this is a successful no-op while voices are active. [force]
  /// stops the device even during active playback without mutating any voice.
  FFI_PLUGIN_EXPORT enum PlayerErrors stopAudioDevice(unsigned int force);

  /// Restart the audio output device previously stopped by stopAudioDevice(),
  /// so existing voices and loaded sounds keep operating. Idempotent: a no-op
  /// if the device is already started.
  FFI_PLUGIN_EXPORT enum PlayerErrors startAudioDevice();

  /// Get the current state of the audio output device. Returns
  /// [AudioDeviceState.audioDeviceUninitialized] if the engine is not
  /// initialized.
  FFI_PLUGIN_EXPORT enum AudioDeviceState getAudioDeviceState();

  /// Test-only hook that sends an interruption through miniaudio's normal
  /// notification callback. This is intentionally absent from the public API.
  FFI_PLUGIN_EXPORT void debugTriggerAudioInterruption(unsigned int began);

  /// List playback devices.
  FFI_PLUGIN_EXPORT void listPlaybackDevices(char **devicesName,
                                             int **deviceId, int **isDefault,
                                             int *n_devices);

  /// Change the playback device.
  ///
  /// [deviceID] the device ID. -1 for default OS output device.
  FFI_PLUGIN_EXPORT enum PlayerErrors changeDevice(int deviceID);

  /// Free the list of playback devices.
  FFI_PLUGIN_EXPORT void freeListPlaybackDevices(char **devicesName,
                                                 int **deviceId,
                                                 int **isDefault,
                                                 int n_devices);

  /// Must be called when there is no more need of the player or when closing
  /// the app.
  FFI_PLUGIN_EXPORT void dispose();

  /// Claim the native engine for a FlutterEngine ahead of initializing it.
  ///
  /// [owner_engine_id] is the FlutterEngine that will own the engine this
  /// initialization creates, or -1 on platforms with no engine-lifecycle
  /// hooks.
  FFI_PLUGIN_EXPORT void prepareEngineInit(int64_t owner_engine_id);

  FFI_PLUGIN_EXPORT void requestEngineShutdown();

  /// The epoch a prepare request must quote to be accepted.
  FFI_PLUGIN_EXPORT uint64_t currentEngineShutdownEpoch();

  /// prepareEngineInit() for a claim that was decided earlier than it is
  /// taken.
  ///
  /// Returns false, changing nothing, when a shutdown has been requested
  /// since [shutdown_epoch] was read.
  FFI_PLUGIN_EXPORT bool prepareEngineInitForRequest(int64_t owner_engine_id,
                                                     uint64_t shutdown_epoch);

  /// Tear the engine down because its owning FlutterEngine is being destroyed
  /// while the process keeps running (the audio_service / add-to-app case).
  ///
  /// Returns false unless [engine_id] still owns the native engine.
  FFI_PLUGIN_EXPORT bool requestEngineTeardownForEngine(int64_t engine_id);

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
  /// Test-only entry points, compiled out of every shipping build. See
  /// `test/engine_lifecycle_test.cpp`. Not visible to ffigen.

  /// Peak number of deferred idle-timeout workers alive at once.
  FFI_PLUGIN_EXPORT int soloudTestIdleTimeoutWorkerPeak();

  FFI_PLUGIN_EXPORT void soloudTestResetIdleTimeoutWorkerPeak();

  /// The policy the current Player is actually running with, as opposed to
  /// the process-global publication.
  FFI_PLUGIN_EXPORT int64_t soloudTestAppliedIdleTimeoutMs();

  /// Set or clear the *engine-level* state callback.
  FFI_PLUGIN_EXPORT void soloudTestSetEngineStateCallback(unsigned int enable);

  /// How many FlutterEngine-owned teardown workers have run to completion.
  FFI_PLUGIN_EXPORT int soloudTestEngineTeardownCompletedCount();

  FFI_PLUGIN_EXPORT void soloudTestLockInitDeinit();

  FFI_PLUGIN_EXPORT void soloudTestUnlockInitDeinit();

  /// Dispatch through the native state-changed bridge, which is what a real
  /// engine event does. Used to prove a retired callable is never invoked.
  FFI_PLUGIN_EXPORT void soloudTestInvokeStateChanged(unsigned int state);

  /// Dispatch through a real BufferStream/PullBufferStream, the way the audio
  /// thread does.
  ///
  /// Returns false when [hash] names no buffer-backed sound.
  FFI_PLUGIN_EXPORT bool soloudTestInvokeStreamCallbacks(unsigned int hash);

  FFI_PLUGIN_EXPORT void soloudTestArmInitBarrier();

  FFI_PLUGIN_EXPORT void soloudTestWaitInitBarrierReached();

  FFI_PLUGIN_EXPORT void soloudTestReleaseInitBarrier();

  /// Drive the exact function MixerOutput holds on its notification thread.
  FFI_PLUGIN_EXPORT void soloudTestInvokeMixerOutput();

  /// True while a Dart callable registered at the current generation may run.
  FFI_PLUGIN_EXPORT int soloudTestCallbacksAreLive();

  /// Whether the Player itself is initialized, as opposed to the
  /// `engine_initialized` flag that prepareEngineInit() also lowers.
  FFI_PLUGIN_EXPORT int soloudTestPlayerIsInited();
#endif

  FFI_PLUGIN_EXPORT int isInited();

  //////////////////////////////////////////////////////////////
  /// Loading sounds and streams

  /// Load a new sound to be played once or multiple times later.
  ///
  /// After loading the file, the [fileLoadedCallback] will call the
  /// Dart function defined with [setDartEventCallback] which gives back
  /// the error and the new hash.
  ///
  /// [completeFileName] the complete file path.
  /// [loadIntoMem] if true Soloud::wav will be used which loads
  /// all audio data into memory. This will be useful when
  /// the audio is short, ie for game sounds, mainly used to prevent
  /// gaps or lags when starting a sound (less CPU, more memory allocated).
  /// If false, Soloud::wavStream will be used and the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [loadIntoMem] = false
  FFI_PLUGIN_EXPORT void loadFile(char *completeFileName, bool loadIntoMem,
                                  uint64_t counter);

  /// Load a new sound stored into [buffer] to be played once or multiple times
  /// later. Mainly used on web because the browsers are not allowed to read
  /// files directly.
  ///
  /// [uniqueName] the unique name of the sound. Used only to have the [hash].
  /// [buffer] the audio data. These contains the audio file bytes.
  /// [length] the length of [buffer].
  /// [loadIntoMem] if true Soloud::wav will be used which loads
  /// all audio data into memory. This will be useful when
  /// the audio is short, ie for game sounds, mainly used to prevent
  /// gaps or lags when starting a sound (less CPU, more memory allocated).
  /// If false, Soloud::wavStream will be used and the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [loadIntoMem] = false
  /// [hash] return the hash of the sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors loadMem(char *uniqueName,
                                              unsigned char *buffer,
                                              int length, int loadIntoMem,
                                              unsigned int *hash);

  /// Load 2 audios, convert to mono if needed, and join them into a single
  /// stereo AudioSource.
  /// [hash] return the hash of the sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors joinTwoSources(char *uniqueName,
                                                     unsigned char *mem1,
                                                     unsigned char *mem2,
                                                     int length1, int length2,
                                                     unsigned int *hash);

  /// Set up an audio stream.
  ///
  /// [maxBufferSize] the max buffer size in **bytes**. When adding audio data
  /// using [addAudioDataStream] and this values is reached, the stream will
  /// be considered ended (likewise we called [setDataIsEnded]). This means
  /// that when playing it, it will stop at that point (if loop is not set).
  ///
  /// **Note:** this parameter doesn't allocate any memory, but it just limits
  /// the amount of data that can be added.
  ///
  /// [bufferingTimeNeeds] the buffering time needed in seconds. If a handle
  /// reaches the current buffer length, it will start to buffer pausing it
  /// and waiting until the buffer will have enough data to cover this time.
  ///
  /// [sampleRate] the sample rate. Usually is 22050 or 44100 (CD quality).
  /// When using [format] as `opus`, the sample rate can be 48000, 24000,
  /// 16000, 12000 or 8000. Whatever the sample rate of the incoming data is,
  /// it will be resampled to this value. So, if you are adding Opus data at
  /// 48 KHz, and you set this to 24000, the data will be resampled to 24 KHz.
  ///
  /// [channels] choose the number of channels. The `opus` format
  /// supports only mono and stereo.
  ///
  /// [format] choose from `f32le`, `s8`, `s16le`, `s32le` and
  /// `opus`. The last one is a special format that uses the Opus codec with
  /// Ogg container. It supports only 48, 24, 16, 12 and 8 KHz sample rates
  /// and mono and stereo.
  ///
  /// [onBufferingCallback] a callback that is called when starting to buffer
  /// (isBuffering = true) and when the buffering is done (isBuffering =
  /// false). The callback is called with the `handle` which triggered the
  /// event and the `time` in seconds.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  setBufferStream(unsigned int *hash, unsigned long maxBufferSize,
                  int bufferingType, double bufferingTimeNeeds,
                  unsigned int sampleRate, unsigned int channels, int format,
                  dartOnBufferingCallback_t onBufferingCallback,
                  dartOnMetadataCallback_t onMetadataCallback);

  /// Resets the buffer of the data stream.
  /// [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors resetBufferStream(unsigned int hash);

  /// Get the time consumed by the stream of a type `BufferingType.RELEASED`
  /// with hash [hash].
  FFI_PLUGIN_EXPORT enum PlayerErrors getStreamTimeConsumed(
      unsigned int hash, float *timeConsumed);

  /// Set the icy metadata integer value. Must be set once before calling
  /// the first time [addAudioDataStream] to be able to get MP3 metadata
  /// of a stream.
  ///
  /// [hash] the hash of the stream sound.
  /// [icyMetaInt] the icy metadata integer value. Default is 16000 which
  /// is the most used value.
  FFI_PLUGIN_EXPORT enum PlayerErrors setBufferIcyMetaInt(unsigned int hash,
                                                          int icyMetaInt);

  /// Set up a pull-based audio stream.
  ///
  /// [audioSizeBytes] the total size in **bytes** of the original encoded or
  /// PCM stream. This is used to compute the total audio duration and to
  /// request the tail chunk for formats where the duration is not in the
  /// header (Ogg Vorbis/Opus).
  ///
  /// [onBufferingCallback] a callback that is called when starting to buffer
  /// and when the buffering is done.
  ///
  /// [onMetadataCallback] a callback that is called when metadata is detected.
  ///
  /// [onMoreDataIsNeededCallback] a callback that is called when the engine
  /// needs more encoded audio data. The parameter is the byte offset in the
  /// original encoded stream.
  ///
  /// [onAudioDurationCallback] a callback that is called once the total audio
  /// duration has been determined.
  FFI_PLUGIN_EXPORT enum PlayerErrors setPullBufferStream(
      unsigned int *hash, unsigned int bufferSizeBytes,
      double bufferTriggerPosition, unsigned int sampleRate,
      unsigned int channels, int format, uint64_t audioSizeBytes,
      dartOnBufferingCallback_t onBufferingCallback,
      dartOnMetadataCallback_t onMetadataCallback,
      dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
      dartOnAudioDurationCallback_t onAudioDurationCallback);

  /// Resets the pull buffer stream.
  FFI_PLUGIN_EXPORT enum PlayerErrors resetPullBufferStream(unsigned int hash);

  /// Add a chunk of audio data to the pull buffer stream.
  /// [offset] the byte offset of this chunk in the original stream, or 0 for
  /// the next sequential chunk.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  addPullBufferDataStream(unsigned int hash, const unsigned char *data,
                          unsigned int aDataLen, uint64_t offset);

  /// Get the decoded time range of the pull buffer stream.
  /// [startTime] returns the start time in seconds of the decoded buffer.
  /// [endTime] returns the end time in seconds of the decoded buffer.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  getPullBufferTimeRange(unsigned int hash, double *startTime,
                         double *endTime);

  FFI_PLUGIN_EXPORT enum PlayerErrors
  addAudioDataStream(unsigned int hash, const unsigned char *data,
                     unsigned int aDataLen);

  /// Set the end of the data stream.
  /// [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors setDataIsEnded(unsigned int hash);

  /// Get the current buffer size in bytes of this sound with hash [hash].
  /// [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors getBufferSize(unsigned int hash,
                                                    unsigned int *sizeInBytes);

  /// Load a new waveform to be played once or multiple times later
  ///
  /// [waveform]  WAVE_SQUARE = 0,
  ///             WAVE_SAW,
  ///             WAVE_SIN,
  ///             WAVE_TRIANGLE,
  ///             WAVE_BOUNCE,
  ///             WAVE_JAWS,
  ///             WAVE_HUMPS,
  ///             WAVE_FSQUARE,
  ///             WAVE_FSAW
  /// [superWave]
  /// [scale]
  /// [detune]
  /// [hash] return hash of the sound
  /// Returns [PlayerErrors.noError] if success
  FFI_PLUGIN_EXPORT enum PlayerErrors loadWaveform(int waveform,
                                                   bool superWave, float scale,
                                                   float detune,
                                                   unsigned int *hash);

  /// Set the scale of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newScale]
  FFI_PLUGIN_EXPORT void setWaveformScale(unsigned int hash, float newScale);

  /// Set the detune of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newDetune]
  FFI_PLUGIN_EXPORT void setWaveformDetune(unsigned int hash, float newDetune);

  /// Set a new frequency of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newFreq]
  FFI_PLUGIN_EXPORT void setWaveformFreq(unsigned int hash, float newFreq);

  /// Set a new frequence of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [superwave]
  FFI_PLUGIN_EXPORT void setSuperWave(unsigned int hash, bool superwave);

  /// Set a new wave form of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newWaveform]   WAVE_SQUARE = 0,
  ///                 WAVE_SAW,
  ///                 WAVE_SIN,
  ///                 WAVE_TRIANGLE,
  ///                 WAVE_BOUNCE,
  ///                 WAVE_JAWS,
  ///                 WAVE_HUMPS,
  ///                 WAVE_FSQUARE,
  ///                 WAVE_FSAW
  FFI_PLUGIN_EXPORT void setWaveform(unsigned int hash, int newWaveform);

  /// Speech the text given
  ///
  /// [textToSpeech]
  /// Returns [PlayerErrors.noError] if success and [handle] sound identifier
  FFI_PLUGIN_EXPORT enum PlayerErrors speechText(char *textToSpeech,
                                                 unsigned int *handle);

  //////////////////////////////////////////////////////////////
  /// Voice control

  /// Switch pause state for an already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid. Unpausing posts an asynchronous device start, so
  /// this never reports [PlayerErrors.audioDeviceFailedToStart].
  FFI_PLUGIN_EXPORT enum PlayerErrors pauseSwitch(unsigned int handle);

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// [pause] the sound handle
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid. Unpausing posts an asynchronous device start, so
  /// this never reports [PlayerErrors.audioDeviceFailedToStart].
  FFI_PLUGIN_EXPORT enum PlayerErrors setPause(unsigned int handle,
                                               bool pause);

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// Return true if paused
  FFI_PLUGIN_EXPORT int getPause(unsigned int handle);

  /// Set a sound's relative play speed.
  /// Setting the value to 0 will cause undefined behavior, likely a crash.
  /// Change the relative play speed of a sample. This changes the effective
  /// sample rate while leaving the base sample rate alone.
  ///
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample rate
  /// is cheaper.
  ///
  /// [handle] the sound handle
  /// [speed] the new speed
  FFI_PLUGIN_EXPORT void setRelativePlaySpeed(unsigned int handle,
                                              float speed);

  /// Get a sound's relative play speed.
  /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.
  ///
  /// [handle] the sound handle
  /// Return the current play speed.
  FFI_PLUGIN_EXPORT float getRelativePlaySpeed(unsigned int handle);

  /// Gets the approximate volume for output per output channel (i.e, per
  /// speaker).
  ///
  /// [channel] the channel.
  /// Return zero for invalid parameters.
  FFI_PLUGIN_EXPORT float getApproximateVolume(unsigned int channel);

  /// Play already loaded sound identified by [hash]
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not paused
  /// [looping] whether to start the sound in looping state.
  /// [loopingStartAt] If looping is enabled, the loop point is, by default,
  /// the start of the stream. The loop start point can be set with this
  /// [loopingStartOffsetAt] Optional exact frame offset to restart looping
  /// from (-1 = inactive).
  /// [loopingEndOffsetAt] Optional exact frame offset to loop before
  /// (-1 = inactive).
  /// [scale] relative playback speed multiplier (1.0f = normal speed).
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors play(
      unsigned int soundHash, unsigned int busId, float volume, float pan,
      bool paused, bool looping, double loopingStartAt, double loopingEndAt,
      int loopingStartOffsetAt, int loopingEndOffsetAt, float scale,
      unsigned int *handle);

  /// Variant of [play] that takes an additional parameter, the time offset
  /// for the sound.
  ///
  /// While the vanilla [play] tries to play sounds as soon as possible,
  /// [playClocked] will delay the start of sounds so that rapidly launched
  /// sounds don't all get clumped to the start of the next outgoing sound
  /// buffer.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [soundTime] your app's "physics time", in seconds. SoLoud will use that
  /// time (as well as the time previously used) to calculate the delay
  /// between two sound effects.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [scale] relative playback speed multiplier (1.0f = normal speed).
  /// [looping] whether the sound loops upon reaching the end.
  /// [loopingStartAt] time position in seconds to restart playback when
  /// looping.
  /// [loopingEndAt] If greater than zero, loop before this time.
  /// [loopingStartOffsetAt] Optional exact frame offset to restart looping
  /// from (-1 = inactive).
  /// [loopingEndOffsetAt] Optional exact frame offset to loop before
  /// (-1 = inactive).
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors playClocked(
      unsigned int soundHash, double soundTime, unsigned int busId,
      float volume, float pan, float scale, bool looping,
      double loopingStartAt, double loopingEndAt, int loopingStartOffsetAt,
      int loopingEndOffsetAt, unsigned int *handle);

  /// Set the number of samples to delay before starting to play a sound.
  ///
  /// This is used internally by [playClocked]. In the unlikely event that
  /// you may want to use it manually, it's available here. Note that calling
  /// this on a "live" voice will cause silence to be inserted at the start
  /// of the next audio buffer.
  ///
  /// [handle] the sound handle
  /// [samples] the number of samples to delay the sound with
  FFI_PLUGIN_EXPORT void setDelaySamples(unsigned int handle,
                                         unsigned int samples);

  /// Get the current stream time of a voice, in seconds.
  ///
  /// [handle] the sound handle
  /// Return the stream time in seconds. 0 if [handle] is invalid.
  FFI_PLUGIN_EXPORT double getStreamTime(unsigned int handle);

  /// Reset the clock used by [playClocked] and [play3dClocked] to the state
  /// as if they were never called.
  ///
  /// The next clocked play will anchor the caller's "physics time" to the
  /// audio clock again (leading by two output buffers).
  FFI_PLUGIN_EXPORT void resetStreamTime();

  /// Get the engine's global stream time, in seconds.
  ///
  /// This is the clock the mixer advances at the start of every output
  /// buffer and the time base used by [playScheduled], [stopScheduled] and
  /// [fadeScheduled]. It only advances while the audio device is mixing.
  ///
  /// Return the engine time in seconds.
  FFI_PLUGIN_EXPORT double getEngineTime();

  /// Engine time of the sample currently reaching the output device: the mix
  /// clock (see [getEngineTime]) minus the render-ahead ring depth. Equals
  /// [getEngineTime] when the render-ahead ring is disabled (the default).
  FFI_PLUGIN_EXPORT double getPlayheadTime();

  /// Estimated output latency in seconds (render-ahead ring depth plus one
  /// device period). 0 when the render-ahead ring is disabled.
  FFI_PLUGIN_EXPORT double getOutputLatency();

  /// Whether the render-ahead ring (the retroactive re-mix prerequisite) is
  /// active. Enabled at init time via `initEngine`'s `renderAheadFrames`.
  FFI_PLUGIN_EXPORT unsigned int isRenderAheadEnabled();

  /// Start playing a sound at an absolute engine time (see [getEngineTime]),
  /// with sample accuracy.
  ///
  /// Unlike [playClocked] there is no anchor and no re-anchor guard, so
  /// sounds can be scheduled arbitrarily far in the future. A time in the
  /// past plays as soon as possible.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [atTime] the absolute engine time, in seconds, at which the sound
  /// should start
  /// [duration] if greater than zero, the sound is automatically stopped
  /// at [atTime] + [duration]
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [scale] relative playback speed multiplier (1.0f = normal speed)
  /// [looping] whether the sound loops upon reaching the end
  /// [loopingStartAt] time position in seconds to restart playback when
  /// looping
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  /// [loopingStartOffsetAt] Optional exact frame offset to restart looping
  /// from (-1 = inactive).
  /// [loopingEndOffsetAt] Optional exact frame offset to loop before
  /// (-1 = inactive).
  FFI_PLUGIN_EXPORT enum PlayerErrors playScheduled(
      unsigned int soundHash, double atTime, double duration,
      unsigned int busId, float volume, float pan, float scale, bool looping,
      double loopingStartAt, double loopingEndAt, int loopingStartOffsetAt,
      int loopingEndOffsetAt, unsigned int *handle);

  /// Stop a sound at an absolute engine time (see [getEngineTime]).
  ///
  /// A time in the past stops the sound immediately.
  ///
  /// [handle] the sound handle
  /// [atTime] the absolute engine time, in seconds, at which the sound
  /// should stop
  FFI_PLUGIN_EXPORT void stopScheduled(unsigned int handle, double atTime);

  /// Fade the volume of a sound starting at an absolute engine time
  /// (see [getEngineTime]).
  ///
  /// The fade goes from the volume the sound has at call time to [to] over
  /// [fadeTime] seconds. If [thenStop] is true, the sound is stopped when
  /// the fade ends (at [atTime] + [fadeTime]).
  ///
  /// [handle] the sound handle
  /// [atTime] the absolute engine time, in seconds, at which the fade
  /// should start. A time in the past starts the fade immediately.
  /// [to] the ending volume of the fade
  /// [fadeTime] the duration of the fade, in seconds
  /// [thenStop] whether to stop the sound when the fade ends
  FFI_PLUGIN_EXPORT void fadeScheduled(unsigned int handle, double atTime,
                                       float to, double fadeTime,
                                       bool thenStop);

  /// Stop already loaded sound identified by [handle] and clear it
  ///
  /// [handle]
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid (for example the voice has already ended).
  FFI_PLUGIN_EXPORT enum PlayerErrors stop(unsigned int handle);

  /// Stop all playing voices without disposing the loaded sounds.
  ///
  /// Each stopped voice triggers the voice-ended callback (dispatched by
  /// SoLoud itself), so Dart is notified for every handle like with [stop].
  FFI_PLUGIN_EXPORT void stopAll();

  /// Stop all voices playing the already loaded sound identified by
  /// [soundHash] without disposing it.
  ///
  /// Each stopped voice triggers the voice-ended callback (dispatched by
  /// SoLoud itself), so Dart is notified for every handle like with [stop].
  FFI_PLUGIN_EXPORT void stopAudioSource(unsigned int soundHash);

  /// Stop all handles of the already loaded sound identified by [hash] and
  /// dispose it
  ///
  /// [soundHash]
  FFI_PLUGIN_EXPORT void disposeSound(unsigned int soundHash);

  /// Dispose all sounds already loaded
  FFI_PLUGIN_EXPORT void disposeAllSound();

  /// Query whether a sound is set to loop.
  ///
  /// [handle]
  /// Returns true if flagged for looping.
  FFI_PLUGIN_EXPORT int getLooping(unsigned int handle);

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [soundHash]
  /// [enable]
  FFI_PLUGIN_EXPORT void setLooping(unsigned int handle, bool enable);

  /// Get sound loop point value.
  ///
  /// [handle]
  /// Returns the time in seconds.
  FFI_PLUGIN_EXPORT double getLoopPoint(unsigned int handle);

  /// Set sound loop point value.
  ///
  /// [handle]
  /// [time] in seconds.
  FFI_PLUGIN_EXPORT void setLoopPoint(unsigned int handle, double time);

  /// Get the sound loop end point value.
  ///
  /// [handle]
  /// Returns the time in seconds, or zero for the natural stream end.
  FFI_PLUGIN_EXPORT double getLoopEndPoint(unsigned int handle);

  /// Set the sound loop end point value.
  ///
  /// [handle]
  /// [time] in seconds, or zero to use the natural stream end.
  FFI_PLUGIN_EXPORT void setLoopEndPoint(unsigned int handle, double time);

  /// Enable or disable visualization
  ///
  /// [enabled] enable or disable it
  /// [windowSize] power of two from 128 to 8192 (default 256)
  /// [kind] 0: wave, 1: FFT, 2: wave and FFT
  /// [channel] -1: merged mono, -2: all channels, >= 0: specific channel index
  FFI_PLUGIN_EXPORT enum PlayerErrors setVisualizationEnabled(bool enabled,
                                                              int windowSize,
                                                              int kind,
                                                              int channel);

  /// Get visualization state
  ///
  /// Return true if enabled
  FFI_PLUGIN_EXPORT int getVisualizationEnabled();

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value will
  /// be decreased with an amplitude between the old and the new value.
  /// This will result on a less shaky visualization.
  ///
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  FFI_PLUGIN_EXPORT void setFftSmoothing(float smooth);

  /// Get the sound length in seconds
  ///
  /// [soundHash] the sound hash
  /// Returns sound length in seconds
  FFI_PLUGIN_EXPORT double getLength(unsigned int soundHash);

  /// Seek playing in [time] seconds
  /// [time]
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  /// NOTE: when seeking an mp3 file loaded using `loadIntoMem`=false
  /// the seek operation is not performed due to lags. This occurs because the
  /// mp3 codec must compute each frame length to gain a new position.
  /// The problem is explained in souloud_wavstream.cpp
  /// in `WavStreamInstance::seek` function.
  ///
  /// This mode is useful ie for background music, not for a music player
  /// where a seek slider for mp3s is a must.
  /// If you need seeking mp3, please, use `loadIntoMem`=true instead
  /// or other audio formats!
  FFI_PLUGIN_EXPORT enum PlayerErrors seek(unsigned int handle, float time);

  /// Get current sound position  in seconds
  ///
  /// [handle] the sound handle
  /// Returns time in seconds
  FFI_PLUGIN_EXPORT double getPosition(unsigned int handle);

  /// Get current Global volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT double getGlobalVolume();

  /// Set current Global volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT enum PlayerErrors setGlobalVolume(float volume);

  /// Get current [handle] volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT double getVolume(unsigned int handle);

  /// Set current [handle] volume
  FFI_PLUGIN_EXPORT enum PlayerErrors setVolume(unsigned int handle,
                                                float volume);

  /// Get a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// Returns the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  FFI_PLUGIN_EXPORT double getPan(unsigned int handle);

  /// Set a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// [pan] the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  FFI_PLUGIN_EXPORT void setPan(unsigned int handle, double pan);

  /// Set the left/right volumes directly.
  /// Note that this does not affect the value returned by getPan.
  ///
  /// [handle] the sound handle.
  /// [panLeft] value for the left pan.
  /// [panRight] value for the right pan.
  FFI_PLUGIN_EXPORT void setPanAbsolute(unsigned int handle, double panLeft,
                                        double panRight);

  /// Check if a handle is still valid.
  ///
  /// [handle] handle to check
  /// Return true if it still exists
  FFI_PLUGIN_EXPORT int getIsValidVoiceHandle(unsigned int handle);

  /// Returns the number of concurrent sounds that are playing at the moment.
  FFI_PLUGIN_EXPORT unsigned int getActiveVoiceCount();

  /// Returns the number of concurrent sounds that are playing a specific audio
  /// source.
  FFI_PLUGIN_EXPORT int countAudioSource(unsigned int soundHash);

  /// Returns the number of voices the application has told SoLoud to play.
  FFI_PLUGIN_EXPORT unsigned int getVoiceCount();

  /// Get a sound's protection state.
  FFI_PLUGIN_EXPORT bool getProtectVoice(unsigned int handle);

  /// Set a sound's protection state.
  ///
  /// Normally, if you try to play more sounds than there are voices,
  /// SoLoud will kill off the oldest playing sound to make room.
  /// This will most likely be your background music. This can be worked
  /// around by protecting the sound.
  /// If all voices are protected, the result will be undefined.
  ///
  /// [handle]  handle to check.
  /// [protect] whether to protect or not.
  FFI_PLUGIN_EXPORT void setProtectVoice(unsigned int handle, bool protect);

  /// Set the inaudible behavior of a live sound. By default,
  /// if a sound is inaudible, it's paused, and will resume when it
  /// becomes audible again. With this function you can tell SoLoud
  /// to either kill the sound if it becomes inaudible, or to keep
  /// ticking the sound even when it's inaudible.
  ///
  /// [handle]  handle to check.
  /// [mustTick] whether to keep ticking or not when the sound becomes
  /// inaudible.
  /// [kill] whether to kill the sound or not when the sound becomes inaudible.
  FFI_PLUGIN_EXPORT void setInaudibleBehavior(unsigned int handle,
                                              bool mustTick, bool kill);

  /// Get the current maximum active voice count.
  FFI_PLUGIN_EXPORT unsigned int getMaxActiveVoiceCount();

  /// Set the current maximum active voice count.
  /// If voice count is higher than the maximum active voice count,
  /// SoLoud will pick the ones with the highest volume to actually play.
  /// [maxVoiceCount] the max concurrent sounds that can be played.
  FFI_PLUGIN_EXPORT void setMaxActiveVoiceCount(unsigned int maxVoiceCount);

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  /// Used to create a new voice group. Returns 0 if not successful.
  FFI_PLUGIN_EXPORT unsigned int createVoiceGroup();

  /// Deallocates the voice group. Does not stop the voices attached to the
  /// voice group.
  ///
  /// [handle] the group handle to destroy.
  FFI_PLUGIN_EXPORT void destroyVoiceGroup(unsigned int handle);

  /// Adds voice handle to the voice group. The voice handles can still be
  /// used separate from the group.
  /// [voiceGroupHandle] the group handle to add the new [voiceHandle].
  /// [voiceHandle] voice handle to add to the [voiceGroupHandle].
  FFI_PLUGIN_EXPORT void addVoiceToGroup(unsigned int voiceGroupHandle,
                                         unsigned int voiceHandle);

  /// Checks if the handle is a valid voice group. Does not care if the
  /// voice group is empty.
  ///
  /// [handle] the group handle to check.
  /// Return true if [handle] is a group handle.
  FFI_PLUGIN_EXPORT int isVoiceGroup(unsigned int handle);

  /// Checks whether a voice group is empty. SoLoud automatically trims
  /// the voice groups of voices that have ended, so the group may be
  /// empty even though you've added valid voice handles to it.
  ///
  /// [handle] group handle to check.
  /// Return true if the group handle doesn't have any voices.
  FFI_PLUGIN_EXPORT int isVoiceGroupEmpty(unsigned int handle);

  /////////////////////////////////////////
  /// faders & oscillators
  /////////////////////////////////////////

  /// Smoothly change the global volume over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadeGlobalVolume(float to, float time);

  /// Smoothly change a channel's volume over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadeVolume(unsigned int handle,
                                                 float to, float time);

  /// Smoothly change a channel's pan setting over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadePan(unsigned int handle, float to,
                                              float time);

  /// Smoothly change a channel's relative play speed over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  fadeRelativePlaySpeed(unsigned int handle, float to, float time);

  /// After specified time, pause the channel.
  FFI_PLUGIN_EXPORT enum PlayerErrors schedulePause(unsigned int handle,
                                                    float time);

  /// After specified time, stop the channel.
  FFI_PLUGIN_EXPORT enum PlayerErrors scheduleStop(unsigned int handle,
                                                   float time);

  /// Set fader to oscillate the volume at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateVolume(unsigned int handle, float from, float to, float time);

  /// Set fader to oscillate the panning at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillatePan(unsigned int handle, float from, float to, float time);

  /// Set fader to oscillate the relative play speed at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateRelativePlaySpeed(unsigned int handle, float from, float to,
                             float time);

  /// Set fader to oscillate the global volume at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors oscillateGlobalVolume(float from,
                                                            float to,
                                                            float time);

  /////////////////////////////////////////
  /// Filters
  /////////////////////////////////////////

  /// Check if the given filter is active or not.
  ///
  /// [soundHash] the sound to check the filter. If this is =0 this function
  /// searches in the global filters.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the
  /// global filters.
  /// [filterType] filter to check.
  /// Returns [PlayerErrors.noError] if no errors and the index of
  /// the given filter (-1 if the filter is not active).
  FFI_PLUGIN_EXPORT enum PlayerErrors
  isFilterActive(unsigned int soundHash, unsigned int busId,
                 enum FilterType filterType, int *index);

  /// Get parameters names of the given filter.
  ///
  /// [filterType] filter to get param names.
  /// Returns [PlayerErrors.noError] if no errors and the list of param names.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  getFilterParamNames(enum FilterType filterType, int *paramsCount,
                      char **names);

  /// Add the filter [filterType] to [soundHash]. If [soundHash]==0 the
  /// filter is added to global filters.
  ///
  /// [soundHash] the sound to add the filter to.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the
  /// global filters.
  /// [filterType] filter to add.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors addFilter(unsigned int soundHash,
                                                unsigned int busId,
                                                enum FilterType filterType);

  /// Remove the filter [filterType] from [soundHash]. If [soundHash]==0 the
  /// filter is removed from the global filters.
  ///
  /// [soundHash] the sound to add the filter to.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the
  /// global filters.
  /// [filterType] filter to remove.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors removeFilter(unsigned int soundHash,
                                                   unsigned int busId,
                                                   enum FilterType filterType);

  /// Set the effect parameter with id [attributeId]
  /// of [filterType] with [value] value.
  ///
  /// [handle] the handle to set the filter to. If equal to 0, the filter is
  /// applyed globally.
  /// [busId] the bus to set the filter to.
  /// If both [handle] and [busId] are =0 this function sets the global
  /// filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute id of the filter to modify.
  /// [value] the value to set the attribute to.
  ///
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors setFilterParams(
      unsigned int handle, unsigned int busId, enum FilterType filterType,
      int attributeId, float value);

  /// Get the effect parameter with id [attributeId] of [filterType].
  ///
  /// [handle] the handle to get the filter to. If equal to 0, it gets the
  /// global filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global
  /// filters.
  /// [filterType] filter to modify a param. Returns the value of param or
  /// 9999.0 if the filter is not found.
  FFI_PLUGIN_EXPORT enum PlayerErrors getFilterParams(
      unsigned int handle, unsigned int busId, enum FilterType filterType,
      int attributeId, float *filterValue);

  /// Fades a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0, it
  /// fades the global filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global
  /// filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [to] value the attribute should go in [time] duration.
  /// [time] the fade slope duration. Returns [PlayerErrors.noError] if
  /// no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  fadeFilterParameter(unsigned int handle, unsigned int busId,
                      enum FilterType filterType, int attributeId, float to,
                      float time);

  /// Oscillate a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0, it
  /// fades the global filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global
  /// filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [from] the starting value the attribute sould start to oscillate.
  /// [to] the ending value the attribute sould end to oscillate.
  /// [time] the fade slope duration.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateFilterParameter(unsigned int handle, unsigned int busId,
                           enum FilterType filterType, int attributeId,
                           float from, float to, float time);

  /////////////////////////////////////////
  /// 3D audio methods
  /////////////////////////////////////////

  /// play3d() is the 3d version of the play() call
  ///
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [looping] whether to start the sound in looping state.
  /// [loopingStartAt] If looping is enabled, the loop point is, by default,
  /// the start of the stream. The loop start point can be set with this
  /// parameter.
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any
  FFI_PLUGIN_EXPORT enum PlayerErrors play3d(
      unsigned int soundHash, unsigned int busId, float posX, float posY,
      float posZ, float velX, float velY, float velZ, float volume,
      bool paused, bool looping, double loopingStartAt, unsigned int *handle);

  /// Play a 3D sound with an optional bounded loop region.
  ///
  /// This additive entry point preserves the ABI of [play3d].
  /// [loopingEndAt] If greater than zero, loop before this time. Zero uses
  /// the natural end of the stream.
  FFI_PLUGIN_EXPORT enum PlayerErrors play3dWithLoopPoints(
      unsigned int soundHash, unsigned int busId, float posX, float posY,
      float posZ, float velX, float velY, float velZ, float volume,
      bool paused, bool looping, double loopingStartAt, double loopingEndAt,
      int loopingStartOffsetAt, int loopingEndOffsetAt, float scale,
      unsigned int *handle);

  /// play3dClocked() is the 3d version of the playClocked() call.
  ///
  /// Instead of panning like with the "2d" version of the call, the 3d
  /// version requires 3d position and optionally velocity vector. Like its
  /// 2d version, this one delays the start of the sound based on the
  /// [soundTime] parameter, so that firing off sounds rapidly won't cause
  /// the sounds to "clump" together at the start of the next sound buffer.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [soundTime] your app's "physics time", in seconds.
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [volume] 1.0f full volume
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors play3dClocked(
      unsigned int soundHash, double soundTime, unsigned int busId,
      float posX, float posY, float posZ, float velX, float velY, float velZ,
      float volume, float scale, bool looping, double loopingStartAt,
      double loopingEndAt, int loopingStartOffsetAt, int loopingEndOffsetAt,
      unsigned int *handle);

  /// play3dScheduled() is the 3d version of the playScheduled() call.
  ///
  /// Instead of panning like with the "2d" version of the call, the 3d
  /// version requires 3d position and optionally velocity vector. Like its
  /// 2d version, this one starts playing a sound at an absolute engine time
  /// (see [getEngineTime]), with sample accuracy.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [atTime] the absolute engine time, in seconds, at which the sound
  /// should start.
  /// [duration] if greater than zero, the sound is automatically stopped at
  /// [atTime] + [duration].
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [volume] 1.0f full volume
  /// [scale] relative playback speed multiplier (1.0f = normal speed)
  /// [looping] whether the sound loops upon reaching the end
  /// [loopingStartAt] time position in seconds to restart playback when
  /// looping
  /// [loopingEndAt] optional exclusive end point for looping
  /// [loopingStartOffsetAt] Optional exact frame offset to restart looping
  /// from (-1 = inactive).
  /// [loopingEndOffsetAt] Optional exact frame offset to loop before
  /// (-1 = inactive).
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors play3dScheduled(
      unsigned int soundHash, double atTime, double duration,
      unsigned int busId, float posX, float posY, float posZ, float velX,
      float velY, float velZ, float volume, float scale, bool looping,
      double loopingStartAt, double loopingEndAt, int loopingStartOffsetAt,
      int loopingEndOffsetAt, unsigned int *handle);

  /// You can set and get the current value of the speed of
  /// sound width the get3dSoundSpeed() and set3dSoundSpeed() functions.
  /// The speed of sound is used to calculate doppler effects in
  /// addition to the distance delay.
  ///
  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  FFI_PLUGIN_EXPORT void set3dSoundSpeed(float speed);

  /// Get the sound speed
  FFI_PLUGIN_EXPORT float get3dSoundSpeed();

  /// You can set the position, at-vector, up-vector and velocity
  /// parameters of the 3d audio listener with one call
  FFI_PLUGIN_EXPORT void set3dListenerParameters(
      float posX, float posY, float posZ, float atX, float atY, float atZ,
      float upX, float upY, float upZ, float velocityX, float velocityY,
      float velocityZ);

  /// You can set the position parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerPosition(float posX, float posY,
                                               float posZ);

  /// You can set the "at" vector parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerAt(float atX, float atY, float atZ);

  /// You can set the "up" vector parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerUp(float upX, float upY, float upZ);

  /// You can set the listener's velocity vector parameter
  FFI_PLUGIN_EXPORT void set3dListenerVelocity(float velocityX,
                                               float velocityY,
                                               float velocityZ);

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call
  FFI_PLUGIN_EXPORT void set3dSourceParameters(unsigned int handle,
                                               float posX, float posY,
                                               float posZ, float velocityX,
                                               float velocityY,
                                               float velocityZ);

  /// You can set the position parameters of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourcePosition(unsigned int handle, float posX,
                                             float posY, float posZ);

  /// You can set the velocity parameters of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceVelocity(unsigned int handle,
                                             float velocityX,
                                             float velocityY,
                                             float velocityZ);

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceMinMaxDistance(unsigned int handle,
                                                   float minDistance,
                                                   float maxDistance);

  /// You can change the attenuation model and rolloff factor parameters
  /// of a live 3d audio source.
  /// The default values are NO_ATTENUATION and 1.
  ///
  /// NO_ATTENUATION          No attenuation
  /// INVERSE_DISTANCE        Inverse distance attenuation model
  /// LINEAR_DISTANCE         Linear distance attenuation model
  /// EXPONENTIAL_DISTANCE    Exponential distance attenuation model
  FFI_PLUGIN_EXPORT void set3dSourceAttenuation(
      unsigned int handle, unsigned int attenuationModel,
      float attenuationRolloffFactor);

  /// You can change the doppler factor of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceDopplerFactor(unsigned int handle,
                                                  float dopplerFactor);

  /////////////////////////////////////////
  /// waveform audio data
  /////////////////////////////////////////

  FFI_PLUGIN_EXPORT enum ReadSamplesErrors
  readSamplesFromFile(const char *filePath, float startTime, float endTime,
                      unsigned long numSamplesNeeded, bool average,
                      float *pSamples);

  FFI_PLUGIN_EXPORT enum ReadSamplesErrors
  readSamplesFromMem(const unsigned char *buffer, unsigned long dataSize,
                     float startTime, float endTime,
                     unsigned long numSamplesNeeded, bool average,
                     float *pSamples);

  /////////////////////////////////////////
  /// Mixing Bus
  /// https://solhsa.com/soloud/mixbus.html
  /// https://solhsa.com/soloud/soloud_20200207.html#mixing-bus
  ///
  /// A mixing bus is a special audio source that plays other audio sources
  /// through it. Useful for grouped volume control, per-bus filtering,
  /// and per-bus visualization (FFT/wave). Busses can also be nested.
  /// Only one instance of a bus can play at a time.
  /// Busses are protected by default and marked as "must tick".
  /////////////////////////////////////////

  /// Create a new mixing bus.
  /// Returns a unique bus ID (>0) to reference this bus in other calls.
  FFI_PLUGIN_EXPORT unsigned int createBus();

  /// Destroy a mixing bus by its ID.
  /// Does not stop voices that were playing through the bus.
  FFI_PLUGIN_EXPORT void destroyBus(unsigned int busId);

  /// Play the bus itself on the main SoLoud engine so it becomes audible.
  /// You must call this before sounds routed through the bus can be heard.
  ///
  /// [busId] the bus ID returned by createBus.
  /// [volume] playback volume (1.0 = full).
  /// [paused] whether to start paused.
  /// [handle] set to the voice handle of the bus, or 0 on error.
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.busIdNotFound] if [busId]
  /// is unknown, [PlayerErrors.failedToStartPlayback] if no voice could be
  /// created for the bus. When [paused] is false the output device is started
  /// asynchronously after the bus voice exists, so this never reports
  /// [PlayerErrors.audioDeviceFailedToStart].
  ///
  /// Note: to play a sound through a bus, the play() function is used with
  /// the bus ID as an argument. See play() for more information.
  FFI_PLUGIN_EXPORT enum PlayerErrors busPlayOnEngine(unsigned int busId,
                                                      float volume,
                                                      bool paused,
                                                      unsigned int *handle);

  /// Set the number of output channels for the bus (default is 2 = stereo).
  ///
  /// [busId] the bus ID.
  /// [channels] number of channels (1 = mono, 2 = stereo, etc.).
  FFI_PLUGIN_EXPORT int busSetChannels(unsigned int busId,
                                       unsigned int channels);

  /// Enable or disable visualization data gathering for this bus.
  /// Must be enabled before calling busCalcFFT, busGetWave,
  /// or busGetApproximateVolume.
  ///
  /// [busId] the bus ID.
  /// [enable] true to enable, false to disable.
  FFI_PLUGIN_EXPORT void busSetVisualizationEnable(unsigned int busId,
                                                   bool enable);

  /// Calculate and return 256 floats of FFT data for this bus.
  /// The data ranges from low to high frequencies.
  /// Visualization must be enabled first with busSetVisualizationEnable.
  ///
  /// [busId] the bus ID.
  /// Returns a pointer to 256 floats, or nullptr if the bus is not found.
  FFI_PLUGIN_EXPORT float *busCalcFFT(unsigned int busId);

  /// Get 256 samples of wave data currently playing through this bus.
  /// Visualization must be enabled first with busSetVisualizationEnable.
  ///
  /// [busId] the bus ID.
  /// Returns a pointer to 256 floats, or nullptr if the bus is not found.
  FFI_PLUGIN_EXPORT float *busGetWave(unsigned int busId);

  /// Get the approximate output volume for a specific channel of this bus.
  /// Useful for VU meters or level indicators.
  /// Visualization must be enabled first.
  ///
  /// [busId] the bus ID.
  /// [channel] the output channel index (0 = left, 1 = right, etc.).
  /// Returns the approximate volume, or 0 if the bus is not found.
  FFI_PLUGIN_EXPORT float busGetApproximateVolume(unsigned int busId,
                                                  unsigned int channel);

  /// Move a live voice (identified by its handle) into this bus.
  /// The voice will be reparented so it plays through the bus.
  /// Useful for dynamically routing sounds in/out of filtered busses.
  ///
  /// [busId] the bus ID.
  /// [voiceHandle] handle of the voice to annex.
  FFI_PLUGIN_EXPORT void busAnnexSound(unsigned int busId,
                                       unsigned int voiceHandle);

  /// Get the number of voices currently playing through this bus.
  ///
  /// [busId] the bus ID.
  /// Returns the active voice count, or 0 if the bus is not found.
  FFI_PLUGIN_EXPORT unsigned int busGetActiveVoiceCount(unsigned int busId);

#ifdef __cplusplus
}
#endif

#endif // BINDINGS_H

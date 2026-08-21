#pragma once

#ifndef PLAYER_H
#define PLAYER_H

#include "active_sound.h"
#include "audiobuffer/audiobuffer.h"
#include "audiobuffer/buffer.h"
#include "audiobuffer/metadata_ffi.h"
#include "enums.h"
#include "filters/filters.h"
#include "soloud/include/soloud.h"
#include "soloud/include/soloud_speech.h"
#include "soloud/src/backend/miniaudio/miniaudio.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct PlaybackDevice
{
  // Owns its name: the `ma_device_info` array it is built from belongs to the
  // miniaudio context and dies with it.
  std::string name;
  unsigned int isDefault;
  unsigned int id;
  ma_device_id deviceId; // Store the actual device ID, not just the index
};

class Player
{
public:
  Player();
  ~Player();

  /// @brief Initialize the player. Must be called before any other player
  /// functions.
  /// @param sampleRate sample rate. Usually is 22050, 44100 (CD quality) or
  /// 48000.
  /// @param bufferSize the audio buffer size. Usually is 2048, but can be also
  /// 512 when low latency is needed for example in games.
  /// @param channels 1)mono, 2)stereo 4)quad 6)5.1 8)7.1
  /// @param deviceID the device ID. -1 for default OS output device.
  /// @param devicePeriodFrames small output device period used when
  /// [renderAheadFrames] enables the render-ahead ring; 0 = default (512).
  /// @param renderAheadFrames depth of the engine-owned render-ahead ring in
  /// frames; 0 (default) disables it and keeps direct-to-device mixing.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors init(unsigned int sampleRate, unsigned int bufferSize,
                    unsigned int channels, int deviceID = -1,
                    bool lowLatency = true, unsigned int devicePeriodFrames = 0,
                    unsigned int renderAheadFrames = 0);

  /// @brief Change the playback device.
  /// @param deviceID the device ID. -1 for default OS output device.
  PlayerErrors changeDevice(int deviceID);

  /// @brief Enumerate the OS playback devices.
  ///
  /// Static because it reads no player state: it spins up its own local
  /// `ma_context` and tears it down before returning. Callers must be able to
  /// enumerate devices while the engine is being created or destroyed on
  /// another thread, so this must not depend on the lifetime of the global
  /// `player` instance nor take the lifecycle lock (which an in-flight
  /// `init()` can hold for the whole audio-device startup).
  static std::vector<PlaybackDevice> listPlaybackDevices();

  /// @brief Set a function callback triggered when a voice is stopped/ended.
  void setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int *));

  /// @brief Set a function callback triggered after a voice stops or becomes
  /// paused and the SoLoud audio mutex has been released.
  void setVoiceInactiveCallback(void (*voiceInactiveCallback)());

  /// @brief Set a function callback triggered when the state of the player
  /// changes.
  void setStateChangedCallback(void (*stateChangedCallback)(unsigned int));

  /// @brief Must be called when there is no more need of the player or when
  /// closing the app.
  void dispose();

  /// @brief Return true if the player has been initialized.
  bool isInited();

  /// @brief Return the active sounds.
  int getSoundsCount();

  /// @brief Returns human readable string of an error.
  /// @param errorCode the error code.
  /// @return a string represented by the PlayerErrors code.
  const std::string getErrorString(PlayerErrors errorCode) const;

  /// @brief Load a new sound to be played once or multiple times later.
  /// @param completeFileName the complete file path + file name.
  /// @param loadIntoMem if true Soloud::wav will be used which loads
  /// all raw audio data into memory. This will be useful when
  /// the audio is short, ie for game sounds, mainly used to prevent
  /// gaps or lags when starting a sound (less CPU, more memory allocated).
  /// (https://solhsa.com/soloud/wav.html)
  /// If false, the audio data is loaded from the given file when
  /// needed (more CPU less memory allocated).
  /// (https://solhsa.com/soloud/wavstream.html)
  /// @param hash return the hash of the sound.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
  ///
  /// NOTE: non-standard OGG files with custom headers may fail to decode
  /// depending on the codec backend used.
  PlayerErrors loadFile(const std::string &completeFileName,
                        const bool loadIntoMem, unsigned int *hash);

  /// @brief Load a new sound stored into [mem] to be played once or multiple
  /// times later. Mainly used on web because the browsers are not allowed to
  /// read files directly.
  /// @param uniqueName the unique name of the sound. Used only to have the
  /// [hash].
  /// @param mem the audio data. These contains the audio file bytes.
  /// @param length the length of [mem].
  /// @param hash return the hash of the sound.
  PlayerErrors loadMem(const std::string &uniqueName, unsigned char *mem,
                       int length, bool loadIntoMem, unsigned int &hash);

  /// @brief Set up an audio stream.
  /// @param hash return the hash of the sound.
  /// @param maxBufferSize the max buffer size in bytes.
  /// @param bufferingType the buffering type.
  /// @param bufferingTimeNeeds time needed for buffering.
  /// @param pcmFormat PCM format configuration.
  /// @param onBufferingCallback callback for buffering events.
  /// @param onMetadataCallback callback for metadata events.
  PlayerErrors
  setBufferStream(unsigned int &hash, unsigned long maxBufferSize,
                  BufferingType bufferingType, SoLoud::time bufferingTimeNeeds,
                  PCMformat pcmFormat = {44100, 2, 4, PCM_F32LE},
                  dartOnBufferingCallback_t onBufferingCallback = nullptr,
                  dartOnMetadataCallback_t onMetadataCallback = nullptr);

  /// @brief Resets the buffer of the data stream.
  /// @param hash the hash of the sound.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors resetBufferStream(unsigned int hash);

  /// @brief Set the mp3 and Flac buffer icy meta int.
  /// @param hash the hash of the sound.
  /// @param icyMetaInt the icy meta int.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors setBufferIcyMetaInt(unsigned int hash, int icyMetaInt);

  /// @brief Get the time consumed by the data stream of type
  /// `BufferingType.RELEASED`.
  /// @param hash the hash of the stream sound.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors getStreamTimeConsumed(unsigned int hash, float *timeConsumed);

  /// @brief Add a chunk of audio data to the buffer stream.
  /// @param hash the hash of the sound.
  /// @param data the audio data to add.
  /// @param aDataLen the length of [data].
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors addAudioDataStream(unsigned int hash, const unsigned char *data,
                                  unsigned int aDataLen);

  /// @brief Set the end of the buffer data stream.
  /// @param hash the hash of the sound.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors setDataIsEnded(unsigned int hash);

  /// @brief Set up a pull-based audio stream.
  /// @param hash return the hash of the sound.
  /// @param bufferSizeBytes the decoded circular buffer size in bytes.
  /// @param bufferTriggerPosition normalized fraction in `[0.0, 1.0]` that
  /// controls when the engine requests more data.
  /// @param sampleRate the sample rate of the decoded audio.
  /// @param channels the number of channels.
  /// @param format the audio format (PCM variants or AUTO).
  /// @param audioSizeBytes total encoded or PCM file size in bytes.
  /// @param onBufferingCallback callback for buffering events.
  /// @param onMetadataCallback callback for metadata events.
  /// @param onMoreDataIsNeededCallback callback for pull data requests.
  /// @param onAudioDurationCallback callback for total duration.
  PlayerErrors setPullBufferStream(
      unsigned int &hash, unsigned int bufferSizeBytes,
      double bufferTriggerPosition, unsigned int sampleRate,
      unsigned int channels, BufferType format, uint64_t audioSizeBytes,
      dartOnBufferingCallback_t onBufferingCallback,
      dartOnMetadataCallback_t onMetadataCallback,
      dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
      dartOnAudioDurationCallback_t onAudioDurationCallback = nullptr);

  /// @brief Resets the pull buffer stream.
  /// @param hash the hash of the sound.
  PlayerErrors resetPullBufferStream(unsigned int hash);

  /// @brief Add a chunk of audio data to the pull buffer stream.
  /// @param hash the hash of the sound.
  /// @param data the audio data to add.
  /// @param aDataLen the length of [data].
  /// @param offset the byte offset of this chunk in the original stream, or 0
  /// for the next sequential chunk.
  PlayerErrors addPullBufferDataStream(unsigned int hash,
                                       const unsigned char *data,
                                       unsigned int aDataLen,
                                       uint64_t offset);

  /// @brief Get the current decoded buffer time range for a pull buffer stream.
  /// @param hash the hash of the sound.
  /// @param startTime returns the start time in seconds of the decoded buffer.
  /// @param endTime returns the end time in seconds of the decoded buffer.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors getPullBufferTimeRange(unsigned int hash,
                                       double *startTime,
                                       double *endTime);

  /// @brief Get the current buffer size in bytes of this sound with hash
  /// [hash].
  /// @param hash the hash of the stream sound.
  /// @param sizeInBytes return the size in bytes.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors getBufferSize(unsigned int hash, unsigned int *sizeInBytes);

  /// @brief Load a new sound which will be generated by the given params.
  /// @param waveform the type of [SoLoud::Soloud::WAVEFORM] to generate.
  /// @param superWave whater this is a superWave.
  /// @param scale if using [superWave] this is its scale.
  /// @param detune if using [superWave] this is its detune.
  /// @param hash the hash code of the new generated sound.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors loadWaveform(int waveform, bool superWave, float scale,
                            float detune, unsigned int &hash);

  /// @brief If this sound is you can change the scale at runtime.
  /// @param soundHash the sound hash to change the scale to.
  /// @param newScale the new scale.
  void setWaveformScale(unsigned int soundHash, float newScale);

  /// @brief If this sound is you can change the detune at runtime.
  /// @param soundHash the sound hash to change the scale to.
  /// @param newDetune the new detune.
  void setWaveformDetune(unsigned int soundHash, float newDetune);

  /// @brief Set the frequency of the given sound.
  /// @param soundHash the hash of the sound.
  /// @param newFreq the new frequency.
  void setWaveformFreq(unsigned int soundHash, float newFreq);

  /// @brief Set the given sound as a super wave.
  /// @param soundHash the sound hash to change the scale to.
  /// @param superwave whether this sound should be a super wave or not.
  void setWaveformSuperwave(unsigned int soundHash, bool superwave);

  /// @brief Set a waveform type to the given sound: see
  /// [SoLoud::Soloud::WAVEFORM] enum.
  /// @param soundHash the sound hash to change the wafeform type.
  /// @param newWaveform the new waveform type.
  void setWaveform(unsigned int soundHash, int newWaveform);

  /// @brief Switch pause state for an already loaded sound identified by
  /// [handle].
  /// @param handle the sound handle
  /// @return [noError] if success, [backendNotInited] if the engine is not
  /// initialized, [soundHandleNotFound] if [handle] is not valid. Unpausing
  /// posts an asynchronous device start, so this never reports
  /// [audioDeviceFailedToStart].
  PlayerErrors pauseSwitch(unsigned int handle);

  /// @brief Pause or unpause already loaded sound identified by [handle].
  /// @param handle the sound handle.
  /// @param pause whether this sound should be paused or not.
  /// @param isUserAction true if this pause/unpause comes from the user
  /// (Dart setPause/pauseSwitch). Automatic buffering pauses pass false so
  /// they do not flip the user-paused flag.
  /// @return [noError] if success, [backendNotInited] if the engine is not
  /// initialized, [soundHandleNotFound] if [handle] is not valid. Unpausing
  /// posts an asynchronous device start, so this never reports
  /// [audioDeviceFailedToStart].
  PlayerErrors setPause(unsigned int handle, bool pause,
                        bool isUserAction = true);

  /// @brief Schedule a deferred pause of the audio device. If no voices
  /// remain active after a short delay, the engine is paused. Requests are
  /// coalesced so that a burst of stop/pause calls results in a single
  /// background pause, giving the audio backend and OS time to stabilize the
  /// audio session (e.g. Control Center on iOS).
  void pauseEngine();

  /// @brief Apply the configured idle policy after a voice may have become
  /// inactive. The lifecycle scheduler performs the authoritative active voice
  /// count check before stopping the device.
  void evaluateAudioDeviceIdle();

  /// @brief Ensure the audio device is started, off the UI thread. Posts an
  /// immediate resume request to the background scheduler so the blocking
  /// native ma_device_start() does not freeze the caller (the UI thread on
  /// the FFI path). Cancels any pending deferred pause. Idempotent: a no-op
  /// at the backend if the device is already started.
  void resumeEngine();

  /// @brief Set how long the audio output device keeps running while the
  /// engine is idle (no active voices) before it is automatically stopped.
  /// This generalizes the deferred idle-pause: instead of a fixed ~500 ms
  /// delay, the caller chooses the delay, disables it entirely, or makes the
  /// device stop as soon as possible.
  ///
  /// [timeoutMs] < 0 keeps the device running indefinitely while idle (a
  /// device-level replacement for playing a silent looping sound; the device
  /// keeps rendering silence and the app keeps its OS audio session alive).
  /// [timeoutMs] == 0 stops the device as soon as possible once idle (still
  /// asynchronously, off the UI thread). [timeoutMs] > 0 keeps the device
  /// running for that many milliseconds after going idle. Any play/unpause
  /// before the deadline cancels the pending stop. The default is 500 ms.
  ///
  /// Switching to an indefinite timeout starts the device immediately (off the
  /// UI thread) if it was stopped; switching to a finite timeout while nothing
  /// is playing schedules the deferred idle-stop. OS-initiated interruptions
  /// (e.g. a phone call) still stop the device regardless.
  /// @param timeoutMs the idle timeout in milliseconds, or a negative value to
  /// keep the device running indefinitely.
  void setAudioDeviceIdleTimeout(int64_t timeoutMs);

  /// Publish the idle-timeout policy process-wide.
  ///
  /// Static and lock-free on purpose: the policy outlives any individual
  /// Player, so publishing it must never wait for the global engine lifecycle
  /// mutex. A Player that cannot consume the update now still picks it up at
  /// its next init().
  static void publishAudioDeviceIdleTimeout(int64_t timeoutMs);

  /// Apply the currently published policy to this Player and post whatever
  /// lifecycle request it implies. The caller must keep this Player alive for
  /// the duration (in practice: hold init_deinit_mutex).
  void applyPublishedAudioDeviceIdleTimeout();

  /// The idle-timeout policy this Player is currently running with, in
  /// milliseconds; negative means the indefinite keep-alive.
  int64_t currentAudioDeviceIdleTimeoutMs() const
  {
    return mIdleTimeoutMs.load(std::memory_order_acquire);
  }

  /// @brief Stop the audio output device without deinitializing the engine.
  /// By default the device is stopped only when there are no active voices.
  /// When [force] is true it is stopped even during active playback. Neither
  /// mode pauses or otherwise mutates voices, and both are idempotent.
  /// @param force whether to stop even when active voices exist.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors stopAudioDevice(bool force = false);

  /// @brief Restart the audio output device previously stopped by
  /// stopAudioDevice(), so existing voices and loaded sounds keep operating.
  /// Idempotent: a no-op if the device is already started.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  PlayerErrors startAudioDevice();

  /// @brief Get the current state of the audio output device.
  /// @return The current [AudioDeviceState]. Returns
  /// [AudioDeviceState.audioDeviceUninitialized] if the engine is not
  /// initialized.
  AudioDeviceState getAudioDeviceState();

  /// @brief Gets the pause state.
  /// @param handle the sound handle.
  /// @return true if paused.
  bool getPause(unsigned int handle);

  /// @brief Set a sound's relative play speed.
  /// Setting the value to 0 will cause undefined behavior, likely a crash.
  /// Change the relative play speed of a sample. This changes the effective
  /// sample rate while leaving the base sample rate alone.
  ///
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample rate is
  /// cheaper.
  /// @param handle the sound handle.
  /// @param speed the new speed.
  void setRelativePlaySpeed(unsigned int handle, float speed);

  /// @brief Get a sound's relative play speed.
  /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.
  /// @param handle the sound handle.
  /// @return the current play speed.
  float getRelativePlaySpeed(unsigned int handle);

  /// @brief Gets the approximate volume for output per output channel (i.e, per speaker).
  /// @param channel the channel.
  /// @return zero for invalid parameters.
  float getApproximateVolume(unsigned int channel);

  /// @brief Play already loaded sound identified by [soundHash].
  /// @param soundHash the unique hash of the sound to play.
  /// @param volume 1.0f full volume.
  /// @param pan 0.0f centered.
  /// @param paused 0 not pause.
  /// @param looping whether to start the sound in looping state.
  /// @param loopingStartAt If looping is enabled, the loop point is, by
  /// default, the start of the stream. The loop start point can be set with
  /// this parameter.
  /// @param loopingEndAt If greater than zero, loop before this time. Zero
  /// uses the natural end of the stream.
  /// @return the handle of the sound, 0 if error.
  PlayerErrors play(unsigned int soundHash, unsigned int &handle,
                    unsigned int busId = 0,
                    float volume = 1.0f, float pan = 0.0f, bool paused = false,
                    bool looping = false, double loopingStartAt = 0.0,
                    double loopingEndAt = 0.0, long long loopingStartOffsetAt = -1,
                    long long loopingEndOffsetAt = -1, float scale = 1.0f);

  /// @brief Variant of [play] that takes an additional parameter, the time
  /// offset for the sound.
  ///
  /// While the vanilla [play] tries to play sounds as soon as possible,
  /// [playClocked] will delay the start of sounds so that rapidly launched
  /// sounds don't all get clumped to the start of the next outgoing sound
  /// buffer.
  /// @param soundHash the unique hash of the sound to play.
  /// @param handle the handle of this new sound.
  /// @param soundTime your app's "physics time", in seconds. SoLoud will use
  /// that time (as well as the time previously used) to calculate the delay
  /// between two sound effects.
  /// @param busId the bus ID to play the sound on. 0 means the main engine.
  /// @param volume 1.0f full volume.
  /// @param pan 0.0f centered.
  /// @param scale relative playback speed multiplier (1.0f = normal speed).
  /// @param looping whether the sound loops upon reaching the end.
  /// @param loopingStartAt time position in seconds to restart playback when looping.
  /// @return the error if any and the [handle] of this new sound.
  PlayerErrors playClocked(unsigned int soundHash, unsigned int &handle,
                           double soundTime, unsigned int busId = 0,
                           float volume = 1.0f, float pan = 0.0f,
                           float scale = 1.0f, bool looping = false,
                           double loopingStartAt = 0.0,
                           double loopingEndAt = 0.0,
                           long long loopingStartOffsetAt = -1,
                           long long loopingEndOffsetAt = -1);

  /// @brief Set the number of samples to delay before starting to play
  /// a sound.
  ///
  /// This is used internally by [playClocked]. In the unlikely event that
  /// you may want to use it manually, it's available here. Note that calling
  /// this on a "live" voice will cause silence to be inserted at the start
  /// of the next audio buffer.
  /// @param handle handle of the sound.
  /// @param samples the number of samples to delay the sound with.
  void setDelaySamples(unsigned int handle, unsigned int samples);

  /// @brief Get the current stream time of a voice, in seconds.
  /// @param handle handle of the sound.
  /// @return the stream time in seconds. 0 if [handle] is invalid.
  double getStreamTime(unsigned int handle);

  /// @brief Reset the clock used by [playClocked] and [play3dClocked] to the
  /// state as if they were never called.
  ///
  /// The next clocked play will anchor the caller's "physics time" to the
  /// audio clock again (leading by two output buffers).
  void resetStreamTime();

  /// @brief Get the engine's global stream time, in seconds.
  ///
  /// This is the clock the mixer advances at the start of every output
  /// buffer and the time base used by [playScheduled], [stopScheduled] and
  /// [fadeScheduled]. It only advances while the audio device is mixing.
  /// @return the engine time in seconds.
  double getEngineTime();

  /// @brief Engine time of the sample currently reaching the output device:
  /// the mix clock minus the render-ahead ring depth. Equals [getEngineTime]
  /// when the render-ahead ring is disabled (the default).
  /// @return the playhead time in seconds.
  double getPlayheadTime();

  /// @brief Estimated output latency in seconds (ring depth plus one device
  /// period). 0 when the render-ahead ring is disabled.
  double getOutputLatency();

  /// @brief Whether the render-ahead ring (retroactive re-mix prerequisite)
  /// is active. Set at [init] time via `renderAheadFrames`.
  bool isRenderAheadEnabled();

  /// @brief Start playing a sound at an absolute engine time (see
  /// [getEngineTime]), with sample accuracy.
  ///
  /// Unlike [playClocked] there is no anchor and no re-anchor guard, so
  /// sounds can be scheduled arbitrarily far in the future. A time in the
  /// past plays as soon as possible.
  /// @param soundHash the unique hash of the sound to play.
  /// @param handle the handle of this new sound.
  /// @param atTime the absolute engine time, in seconds, at which the sound
  /// should start.
  /// @param duration if greater than zero, the sound is automatically
  /// stopped at [atTime] + [duration].
  /// @param busId the bus ID to play the sound on. 0 means the main engine.
  /// @param volume 1.0f full volume.
  /// @param pan 0.0f centered.
  /// @param scale relative playback speed multiplier (1.0f = normal speed).
  /// @param looping whether the sound loops upon reaching the end.
  /// @param loopingStartAt time position in seconds to restart playback when looping.
  /// @return the error if any and the [handle] of this new sound.
  PlayerErrors playScheduled(unsigned int soundHash, unsigned int &handle,
                             double atTime, double duration = 0.0,
                             unsigned int busId = 0,
                             float volume = 1.0f, float pan = 0.0f,
                             float scale = 1.0f, bool looping = false,
                             double loopingStartAt = 0.0,
                             double loopingEndAt = 0.0,
                             long long loopingStartOffsetAt = -1,
                             long long loopingEndOffsetAt = -1);

  /// @brief Stop a sound at an absolute engine time (see [getEngineTime]).
  ///
  /// A time in the past stops the sound immediately.
  /// @param handle handle of the sound.
  /// @param atTime the absolute engine time, in seconds, at which the sound
  /// should stop.
  void stopScheduled(unsigned int handle, double atTime);

  /// @brief Fade the volume of a sound starting at an absolute engine time
  /// (see [getEngineTime]).
  ///
  /// The fade goes from the volume the sound has at call time to [to] over
  /// [fadeTime] seconds. If [thenStop] is true, the sound is stopped when
  /// the fade ends (at [atTime] + [fadeTime]).
  /// @param handle handle of the sound.
  /// @param atTime the absolute engine time, in seconds, at which the fade
  /// should start. A time in the past starts the fade immediately.
  /// @param to the ending volume of the fade.
  /// @param fadeTime the duration of the fade, in seconds.
  /// @param thenStop whether to stop the sound when the fade ends.
  void fadeScheduled(unsigned int handle, double atTime, float to,
                     double fadeTime, bool thenStop);

  /// @brief Stop already loaded sound identified by [handle] and clear it.
  /// @param handle handle of the sound.
  /// @return [noError] if success, [backendNotInited] if the engine is not
  /// initialized, [soundHandleNotFound] if [handle] is not valid.
  PlayerErrors stop(unsigned int handle);

  /// @brief Stop all playing voices without disposing the loaded sounds.
  /// Each stopped voice triggers the voice-ended callback, which removes
  /// the handle from the internal sounds list and notifies Dart.
  void stopAll();

  /// @brief Stop all voices playing the sound identified by [soundHash]
  /// without disposing the sound. Each stopped voice triggers the
  /// voice-ended callback, which removes the handle from the internal
  /// sounds list and notifies Dart.
  /// @param soundHash hash of the sound.
  void stopAudioSource(unsigned int soundHash);

  /// @brief Remove the unique [handle] form the list of internal sounds.
  /// @param handle handle of the sound.
  void removeHandle(unsigned int handle);

  /// @brief Stop all handles of the already loaded sound identified by
  /// [soundHash] and clear it.
  /// @param soundHash hash of the sound.
  void disposeSound(unsigned int soundHash);

  /// @brief Dispose all sounds already loaded.
  void disposeAllSound();

  /// @brief Clear every native->Dart callback pointer held by the player.
  void clearDartCallbackRegistrations();

  /// @brief Ask whether a sound is set to loop or not.
  bool getLooping(unsigned int handle);

  /// @brief This function can be used to set a sample to play on repeat,
  /// instead of just playing once.
  /// @param handle handle of the sound.
  /// @param enable whether to enable looping or not.
  void setLooping(unsigned int handle, bool enable);

  /// @brief Get sound loop point value.
  /// @param handle handle of the sound.
  /// @return the time in seconds.
  double getLoopPoint(unsigned int handle);

  /// @brief Set sound loop point value.
  /// @param handle handle of the sound.
  /// @param time in seconds.
  void setLoopPoint(unsigned int handle, double time);

  /// @brief Get the sound loop end point value.
  /// @param handle handle of the sound.
  /// @return the time in seconds, or zero for the natural stream end.
  double getLoopEndPoint(unsigned int handle);

  /// @brief Set the sound loop end point value.
  /// @param handle handle of the sound.
  /// @param time in seconds, or zero to use the natural stream end.
  void setLoopEndPoint(unsigned int handle, double time);

  /// @brief Speech the given text.
  /// @param textToSpeech the text to be spoken.
  /// @param handle handle of the sound. Set to -1 if error.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  /// TODO(marco): add other T2S parameters
  PlayerErrors textToSpeech(const std::string &textToSpeech,
                            unsigned int &handle);

  /// @brief Enable or disable visualization
  /// @param enabled setting this to true will enable to get wave and FFT data.
  void setVisualizationEnabled(bool enabled);

  /// @brief Returns true if the visualization is enable. If so the wave and FFT
  /// data che be get with `calcFFT()` and `getWave()`.
  bool isVisualizationEnabled();

  /// @brief Calculates FFT of the currently playing sound.
  /// @return a 256 float pointer to the result.
  float *calcFFT(bool *isTheSameAsBefore);

  /// @brief Gets 256 samples of the currently playing sound.
  /// @return a 256 float pointer to the result.
  float *getWave(bool *isTheSameAsBefore);

  /// @brief get the sound length in seconds.
  /// @param soundHash the sound hash.
  /// @return returns sound length in seconds.
  double getLength(unsigned int soundHash);

  /// @brief Seek playing in seconds.
  /// @param handle the sound handle.
  /// @param time the time to seek in seconds.
  /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
  ///
  /// WARNING: when seeking an mp3 file loaded using `loadIntoMem`=false
  /// the seek operation is not performed due to the problem explained
  /// in souloud_wavstream.cpp in `WavStreamInstance::seek` function.
  PlayerErrors seek(SoLoud::handle handle, float time);

  /// @brief Get current sound position in seconds.
  /// @return time in seconds.
  double getPosition(SoLoud::handle handle);

  /// @brief Get current global volume.
  /// @return the volume.
  float getGlobalVolume();

  /// @brief Set the global volume for all the sounds.
  /// @param volume the new volume to set.
  void setGlobalVolume(float volume);

  /// @brief Get current [handle] volume.
  /// @return the volume.
  float getVolume(SoLoud::handle handle);

  /// @brief Set the [handle] volume.
  /// @param handle the sound handle.
  /// @param volume the new volume to set.
  void setVolume(SoLoud::handle handle, float volume);

  /// @brief Get a sound's current pan setting.
  /// @param handle the sound handle.
  /// @return the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  float getPan(SoLoud::handle handle);

  /// @brief Set a sound's current pan setting.
  /// @param handle the sound handle.
  /// @param pan the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  void setPan(SoLoud::handle handle, float pan);

  /// @brief Set the left/right volumes directly.
  /// Note that this does not affect the value returned by getPan.
  /// @param handle the sound handle.
  /// @param panLeft value for the left pan.
  /// @param panRight value for the right pan.
  void setPanAbsolute(SoLoud::handle handle, float panLeft, float panRight);

  /// @brief Check if a handle is still valid.
  /// @param handle handle to check.
  /// @return true if it still exists.
  bool isValidHandle(SoLoud::handle handle);

  /// @brief Return the number of active voices summing up all the handles of
  /// each sound. The difference between this function and getActiveVoiceCount()
  /// is that getActiveVoiceCount() returns the number of active removing those
  /// that are paused or with a very low volume, while this function returns the
  /// total number of active voices.
  unsigned int getActiveVoiceCount_internal();

  /// @brief Returns the number of concurrent sounds that are playing a specific
  /// audio source.
  int countAudioSource(unsigned int soundHash);

  /// @brief Returns the number of voices the application has told SoLoud to
  /// play.
  unsigned int getVoiceCount();

  /// @brief Get a sound's protection state.
  bool getProtectVoice(SoLoud::handle handle);

  /// @brief Set a sound's protection state.
  /// Normally, if you try to play more sounds than there are voices,
  /// SoLoud will kill off the oldest playing sound to make room.
  /// This will most likely be your background music. This can be worked
  /// around by protecting the sound.
  /// If all voices are protected, the result will be undefined.
  /// @param handle  handle to check.
  /// @param protect whether to protect or not.
  ///
  /// NOTE: patched with
  /// https://github.com/jarikomppa/soloud/issues/298
  void setProtectVoice(SoLoud::handle handle, bool protect);

  /// @brief Set the inaudible behavior of a live sound. By default,
  /// if a sound is inaudible, it's paused, and will resume when it
  /// becomes audible again. With this function you can tell SoLoud
  /// to either kill the sound if it becomes inaudible, or to keep
  /// ticking the sound even if it's inaudible.
  void setInaudibleBehavior(SoLoud::handle handle, bool mustTick, bool kill);

  /// @brief Get the current maximum active voice count.
  unsigned int getMaxActiveVoiceCount();

  /// @brief set the current maximum active voice count.
  /// If voice count is higher than the maximum active voice count,
  /// SoLoud will pick the ones with the highest volume to actually play.
  /// @param maxVoiceCount the max concurrent sounds that can be played.
  ///
  /// NOTE: The number of concurrent voices is limited, as having unlimited
  /// voices would cause performance issues, as well as lead to unnecessary
  /// clipping. The default number of concurrent voices is 16, but this can be
  /// adjusted at runtime. The hard maximum number is 4095, but if more are
  /// required, SoLoud can be modified to support more. But seriously, if you
  /// need more than 4095 sounds at once, you're probably going to make some
  /// serious changes in any case.
  void setMaxActiveVoiceCount(unsigned int maxVoiceCount);

  /// @brief Find a sound by its handle.
  /// @param handle the handle to search.
  /// @return If not found, return nullptr.
  ActiveSound *findByHandle(SoLoud::handle handle);

  /// @brief Find a sound by its handle.
  /// @param hash the hash to search.
  /// @return If not found, return nullptr.
  ActiveSound *findByHash(unsigned int hash);

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  /// @brief Used to create a new voice group. Returns 0 if not successful.
  unsigned int createVoiceGroup();

  /// @brief Deallocates the voice group. Does not stop the voices attached to
  /// the voice group.
  /// @param handle the group handle to destroy.
  void destroyVoiceGroup(SoLoud::handle handle);

  /// @brief Adds voice handle to the voice group. The voice handles can still
  /// be used separate from the group.
  /// @param voiceGroupHandle the group handle to add the new [handle].
  /// @param voiceHandle voice handle to add to the [voiceGroupHandle].
  void addVoiceToGroup(SoLoud::handle voiceGroupHandle,
                       SoLoud::handle voiceHandle);

  /// @brief Checks if the handle is a valid voice group. Does not care if the
  /// voice group is empty.
  /// @param handle the group handle to check.
  /// @return true if [handle] is a group handle.
  bool isVoiceGroup(SoLoud::handle handle);

  /// @brief Checks whether a voice group is empty. SoLoud automatically trims
  /// the voice groups of voices that have ended, so the group may be empty even
  /// though you've added valid voice handles to it.
  /// @param handle group handle to check.
  /// @return true if the group handle doesn't have any voices.
  bool isVoiceGroupEmpty(SoLoud::handle handle);

  /////////////////////////////////////////
  /// faders & oscillators
  /////////////////////////////////////////

  /// @brief Smoothly change the global volume over specified time.
  /// @param to the volume to fade to.
  /// @param time the time in seconds to change the volume.
  void fadeGlobalVolume(float to, float time);

  /// @brief Smoothly change the sound volume over specified time.
  /// @param handle the sound handle.
  /// @param to the volume to fade to.
  /// @param time the time in seconds to change the volume.
  void fadeVolume(SoLoud::handle handle, float to, float time);

  /// @brief Smoothly change a channel's pan setting over specified time.
  /// @param handle the sound handle.
  /// @param to the pan value to fade to.
  /// @param time the time in seconds to change the pan.
  void fadePan(SoLoud::handle handle, float to, float time);

  /// @brief Smoothly change a channel's relative play speed over specified
  /// time.
  /// @param handle the sound handle.
  /// @param to the speed value to fade to.
  /// @param time the time in seconds to change the speed.
  void fadeRelativePlaySpeed(SoLoud::handle handle, float to, float time);

  /// @brief After specified time, pause the channel.
  /// @param handle the sound handle.
  /// @param time the time in seconds to pause.
  void schedulePause(SoLoud::handle handle, float time);

  /// @brief After specified time, pause the channel.
  /// @param handle the sound handle.
  /// @param time the time in seconds to pause.
  void scheduleStop(SoLoud::handle handle, float time);

  /// @brief Set fader to oscillate the volume at specified frequency.
  /// @param handle the sound handle.
  /// @param from the lowest value for the oscillation.
  /// @param to the highest value for the oscillation.
  /// @param time the time in seconds to oscillate.
  void oscillateVolume(SoLoud::handle handle, float from, float to, float time);

  /// @brief Set fader to oscillate the panning at specified frequency.
  /// @param handle the sound handle.
  /// @param from the lowest value for the oscillation.
  /// @param to the highest value for the oscillation.
  /// @param time the time in seconds to oscillate.
  void oscillatePan(SoLoud::handle handle, float from, float to, float time);

  /// @brief Set fader to oscillate the relative play speed at specified
  /// frequency.
  /// @param handle the sound handle.
  /// @param from the lowest value for the oscillation.
  /// @param to the highest value for the oscillation.
  /// @param time the time in seconds to oscillate.
  void oscillateRelativePlaySpeed(SoLoud::handle handle, float from, float to,
                                  float time);

  /// @brief Set fader to oscillate the global volume at specified frequency.
  /// @param from the lowest value for the oscillation.
  /// @param to the highest value for the oscillation.
  /// @param time the time in seconds to oscillate.
  void oscillateGlobalVolume(float from, float to, float time);

  /////////////////////////////////////////
  /// 3D audio
  /////////////////////////////////////////

  /// @brief apply change to voices when changing some 3d audio params.
  void update3dAudio();

  /// @brief play3d() is the 3d version of the play() call.
  /// @param looping whether to start the sound in looping state.
  /// @param loopingStartAt If looping is enabled, the loop point is, by
  /// default, the start of the stream. The loop start point can be set with
  /// this parameter.
  /// @param loopingEndAt If greater than zero, loop before this time. Zero
  /// uses the natural end of the stream.
  /// @return the handle of the sound, 0 if error.
  PlayerErrors play3d(unsigned int soundHash, unsigned int &handle,
                      unsigned int busId = 0,
                      float posX = 0.0f, float posY = 0.0f, float posZ = 0.0f,
                      float velX = 0.0f, float velY = 0.0f, float velZ = 0.0f,
                      float volume = 1.0f, bool paused = 0,
                      bool looping = false, double loopingStartAt = 0.0,
                      double loopingEndAt = 0.0, long long loopingStartOffsetAt = -1,
                      long long loopingEndOffsetAt = -1, float scale = 1.0f);

  /// @brief play3dClocked() is the 3d version of the playClocked() call.
  ///
  /// Instead of panning like with the "2d" version of the call, the 3d
  /// version requires 3d position and optionally velocity vector. Like its
  /// 2d version, this one delays the start of the sound based on the
  /// [soundTime] parameter, so that firing off sounds rapidly won't cause
  /// the sounds to "clump" together at the start of the next sound buffer.
  /// @param soundHash the unique hash of the sound to play.
  /// @param handle the handle of this new sound.
  /// @param soundTime your app's "physics time", in seconds.
  /// @param busId the bus ID to play the sound on. 0 means the main engine.
  /// @param posX, posY, posZ the audio source position coordinates.
  /// @param velX, velY, velZ the audio source velocity.
  /// @param volume 1.0f full volume.
  /// @param scale relative playback speed multiplier (1.0f = normal speed).
  /// @return the error if any and the [handle] of this new sound.
  PlayerErrors play3dClocked(unsigned int soundHash, unsigned int &handle,
                             double soundTime, unsigned int busId = 0,
                             float posX = 0.0f, float posY = 0.0f,
                             float posZ = 0.0f, float velX = 0.0f,
                             float velY = 0.0f, float velZ = 0.0f,
                             float volume = 1.0f, float scale = 1.0f,
                             bool looping = false, double loopingStartAt = 0.0,
                             double loopingEndAt = 0.0,
                             long long loopingStartOffsetAt = -1,
                             long long loopingEndOffsetAt = -1);

  /// You can set and get the current value of the speed of
  /// sound width the get3dSoundSpeed() and set3dSoundSpeed() functions.
  /// The speed of sound is used to calculate doppler effects in
  /// addition to the distance delay.

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  void set3dSoundSpeed(float speed);
  float get3dSoundSpeed();

  void set3dListenerParameters(float posX, float posY, float posZ, float atX,
                               float atY, float atZ, float upX, float upY,
                               float upZ, float velocityX, float velocityY,
                               float velocityZ);
  void set3dListenerPosition(float posX, float posY, float posZ);
  void set3dListenerAt(float atX, float atY, float atZ);
  void set3dListenerUp(float upX, float upY, float upZ);
  void set3dListenerVelocity(float velocityX, float velocityY, float velocityZ);

  void set3dSourceParameters(unsigned int handle, float posX, float posY,
                             float posZ, float velocityX, float velocityY,
                             float velocityZ);
  void set3dSourcePosition(unsigned int handle, float posX, float posY,
                           float posZ);
  void set3dSourceVelocity(unsigned int handle, float velocityX,
                           float velocityY, float velocityZ);
  void set3dSourceMinMaxDistance(unsigned int handle, float minDistance,
                                 float maxDistance);
  void set3dSourceAttenuation(unsigned int handle,
                              unsigned int attenuationModel,
                              float attenuationRolloffFactor);
  void set3dSourceDopplerFactor(unsigned int handle, float dopplerFactor);

  /////////////////////////////////////////
  /// Mixing Bus
  /// https://solhsa.com/soloud/mixbus.html
  /// https://solhsa.com/soloud/soloud_20200207.html#mixing-bus
  /////////////////////////////////////////

  unsigned int createBus();
  void destroyBus(unsigned int busId);
  /// @brief Play the bus itself on the main SoLoud engine so it becomes
  /// audible.
  /// @param busId the bus ID returned by `createBus()`.
  /// @param volume playback volume (1.0 = full).
  /// @param paused whether to start paused.
  /// @param handle set to the voice handle of the bus, or zero on error.
  /// @return [noError] if success, [backendNotInited] if the engine is not
  /// initialized, [busIdNotFound] if [busId] is unknown,
  /// [failedToStartPlayback] if no voice could be created. When [paused] is
  /// false the output device is started asynchronously after the bus voice
  /// exists, so this never reports [audioDeviceFailedToStart].
  PlayerErrors busPlayOnEngine(unsigned int busId, float volume, bool paused,
                               unsigned int &handle);
  int busSetChannels(unsigned int busId, unsigned int channels);
  void busSetVisualizationEnable(unsigned int busId, bool enable);
  float *busCalcFFT(unsigned int busId);
  float *busGetWave(unsigned int busId);
  float busGetApproximateVolume(unsigned int busId, unsigned int channel);
  void busAnnexSound(unsigned int busId, unsigned int voiceHandle);
  unsigned int busGetActiveVoiceCount(unsigned int busId);
  BusData *findBusData(unsigned int busId);

public:
  /// all the sounds loaded
  std::vector<std::unique_ptr<ActiveSound>> sounds;

  /// True when the backend is initialized. This is read by the FFI thread,
  /// lifecycle scheduler, and teardown path.
  std::atomic<bool> mInited{false};

  /// main SoLoud engine
  SoLoud::Soloud soloud;

  /// speech object
  SoLoud::Speech speech;

  /// Global filters
  Filters mFilters;

  unsigned int mSampleRate;

  unsigned int mChannels;

private:
  std::mutex remove_handle_mutex;
  mutable std::recursive_mutex sounds_mutex; // Protects the sounds vector (recursive to avoid deadlock in destructors)
  unsigned int mBufferSize;

  std::map<unsigned int, BusData> busMap;
  unsigned int busIdCounter = 0;

  // Background scheduler for deferred engine pause and asynchronous engine
  // resume. Started lazily on init() and stopped on dispose() so that the
  // global Player object can be recreated by bindings.cpp without leaving a
  // stray background thread. The same thread handles both the deferred device
  // stop (pause) and the immediate device start (resume) so neither native
  // ma_device_stop()/ma_device_start() call ever blocks the UI thread.
  std::thread mPauseThread;
  std::mutex mPauseMutex;
  // Serializes the actual blocking device operations performed by both the
  // scheduler and the explicit lifecycle APIs.
  std::mutex mDeviceLifecycleOperationMutex;
  std::condition_variable mPauseCv;
  enum class DeviceLifecycleRequest : uint8_t {
    none,
    start,
    interruptionStop,
    idleStop,
  };
  // The pending request and generation are protected by mPauseMutex. Requests
  // normally replace older intent and advance the generation, allowing a
  // delayed idle stop to detect that it has become stale without maintaining a
  // command queue. Immediate requests have priority over idle work.
  DeviceLifecycleRequest mPendingDeviceRequest =
      DeviceLifecycleRequest::none;
  uint64_t mDeviceRequestGeneration = 0;
  // Protected by mPauseMutex.
  //
  // mImmediateDeviceRequestInFlight identifies a start/interruption operation
  // that has been dequeued but has not completed.
  //
  // mIdleStopRequestedAfterImmediateOperation records idle policy work that
  // arrived after an immediate operation was already pending or in flight.
  // Such idle work must not invalidate the immediate operation.
  DeviceLifecycleRequest mImmediateDeviceRequestInFlight =
      DeviceLifecycleRequest::none;
  bool mIdleStopRequestedAfterImmediateOperation = false;
  // Protected by mPauseMutex.
  //
  // Records that playback recovery requested a start while an interruption
  // stop was pending or in flight. The start must run after the interruption
  // stop completes, provided the interruption has ended.
  bool mStartRequestedAfterInterruptionStop = false;
  bool mStopPauseThread = false;
  // False before initialization is complete and from the first step of
  // shutdown onward. Lifecycle entry points use this to reject work that
  // could otherwise race with scheduler teardown or backend destruction.
  std::atomic<bool> mLifecycleRequestsAccepted{false};
  // True between OS interruption-began and interruption-ended notifications.
  // Start requests are deferred during this interval; interruption recovery
  // reevaluates active playback and idle-timeout policy.
  std::atomic<bool> mInterruptionActive{false};
  std::mutex mInterruptionMutex;
  bool mPauseThreadRunning = false;
  /// How long the device keeps running while idle before the deferred
  /// idle-pause stops it (see setAudioDeviceIdleTimeout). A negative value
  /// keeps the device running indefinitely (the idle-pause never stops it);
  /// 0 stops it as soon as possible; a positive value is the delay in
  /// milliseconds. Read by the scheduler thread, written from the FFI thread.
  std::atomic<int64_t> mIdleTimeoutMs;

  void pauseEngineScheduler();
  /// Apply a pause/unpause to [handle] and post the matching device
  /// lifecycle request. [isUserAction] is false for automatic buffering
  /// pauses, which must not flip the user-paused flag.
  void applyPauseState(unsigned int handle, bool pause, bool isUserAction);
  PlayerErrors performAudioDeviceStart();
  PlayerErrors performAudioDeviceStop(bool explicitRequest);

  /// Report that an *automatic* device start failed after rebuild/retry.
  ///
  /// The synchronous playback APIs hand device startup to the scheduler and
  /// return before it runs, so they cannot report this. Without an event the
  /// app is left with valid unpaused voices and no output and no way to find
  /// out. Explicit starts return the error to their caller instead and do not
  /// go through here.
  void reportAutomaticDeviceStartFailure();

  /// The generation a later cancellation can be compared against.
  ///
  /// A direct device operation takes this *before* it observes the state it
  /// decides on (active voices, current device state, the interruption latch).
  /// Anything posted from that point on carries a newer generation, which is
  /// what makes "newer than my decision" decidable rather than a guess about
  /// timing.
  uint64_t currentDeviceRequestGeneration();

  /// Cancel pending lifecycle work that predates [token].
  ///
  /// Returns false, cancelling nothing, when a start or interruption stop has
  /// been posted since [token] was taken. Such a request expresses intent that
  /// is newer than the operation asking to cancel, and erasing it is how an
  /// unpaused voice ends up with a stopped device, or a genuine OS interruption
  /// ends up ignored.
  ///
  /// Newer *idle* work is still cancelled: it only ever asks for the device to
  /// stop, so no direct operation loses meaning by discarding it, and the idle
  /// timeout is re-armed from the current voice state afterwards anyway.
  bool cancelSupersededDeviceRequests(uint64_t token);

  /// Unconditional cancellation, for the paths that own the whole lifecycle
  /// state and have nothing concurrent to preserve (scheduler start/stop).
  void invalidatePendingDeviceRequest();
  bool isDeviceRequestCurrent(uint64_t generation);
  bool requestDeviceLifecycle(DeviceLifecycleRequest request);
  void startPauseEngineScheduler();
  void stopPauseEngineScheduler();
  void stopDeviceAndDestroyAllSounds();
  void handleAudioInterruption(bool began);
  static void audioInterruptionCallback(void *context, bool began);
};

#endif // PLAYER_H

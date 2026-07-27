/// header file used by ffiGen to generate
/// lib/flutter_soloud_bindings_ffi_TMP.dart

// copy here the definition you would like to generate.
// go into "flutter_soloud" dir from the root project dir
// and run:
//
// dart run ffigen --config ffigen.yaml
// to generate [flutter_soloud_FFIGEN.dart], or if it complains for some reason:
// export CPATH="$(clang -v 2>&1 | grep "Selected GCC installation" | rev | cut -d' ' -f1 | rev)/include";  dart run ffigen --config ffigen.yaml
//
// the generated code will be placed into flutter_soloud_FFIGEN.dart
// copy the generated definition into flutter_soloud_bindings_ffi.dart

#include <stdint.h>

#include <stdbool.h>

#include "enums.h"
#include "audiobuffer/metadata_ffi.h"

#define FFI_PLUGIN_EXPORT

//--------------------- copy here the new functions to generate

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
/// Returns the voice handle for the bus, or 0 on error.
FFI_PLUGIN_EXPORT unsigned int busPlayOnEngine(unsigned int busId,
                                               float volume, bool paused);

/// Play a loaded sound (identified by [soundHash]) through a mixing bus.
/// The sound must have been previously loaded via loadFile/loadMem.
///
/// [busId] the bus to route the sound through.
/// [soundHash] hash of the loaded audio source.
/// [volume] playback volume.
/// [pan] panning (-1 left, 0 center, 1 right).
/// [paused] whether to start paused.
/// Returns the voice handle, or 0 on error.
FFI_PLUGIN_EXPORT unsigned int busPlay(unsigned int busId,
                                       unsigned int soundHash, float volume,
                                       float pan, bool paused);

/// Set up a pull-based audio stream.
///
/// [bufferSizeBytes] the decoded circular buffer size in bytes.
/// [bufferTriggerPosition] normalized fraction in `[0.0, 1.0]` that controls
/// when the engine requests more data.
/// [sampleRate] the sample rate of the decoded audio.
/// [channels] the number of channels.
/// [format] the audio format (PCM variants or AUTO).
/// [audioSizeBytes] total encoded or PCM file size in bytes.
/// [onBufferingCallback] a callback that is called when starting to buffer
/// and when the buffering is done.
/// [onMetadataCallback] a callback that is called when metadata is detected.
/// [onMoreDataIsNeededCallback] a callback that is called when the engine
/// needs more encoded audio data. The parameter is the byte offset in the
/// original encoded stream.
/// [onAudioDurationCallback] a callback that is called once the total audio
/// duration is known.
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
getPullBufferTimeRange(unsigned int hash, double *startTime, double *endTime);

/// Set the end of the pull buffer data stream.
FFI_PLUGIN_EXPORT enum PlayerErrors setPullBufferDataIsEnded(unsigned int hash);

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

/// Start capturing the master mixer output.
///
/// [format] the output format from the MixerOutputFormat enum.
/// [sampleRate] desired sample rate, or -1 to use the engine sample rate.
/// [channels] desired channel count, or -1 to use the engine channel count.
/// [bufferSizeBytes] size of the circular buffer in bytes.
/// [notificationThresholdBytes] number of available bytes before a callback is fired.
/// Returns [PlayerErrors.noError] if success.
FFI_PLUGIN_EXPORT enum PlayerErrors startMixerCapture(
    int format, int sampleRate, int channels,
    uint64_t bufferSizeBytes,
    uint64_t notificationThresholdBytes);

/// Stop capturing the master mixer output.
FFI_PLUGIN_EXPORT void stopMixerCapture();

/// Returns 1 if mixer capture is running, 0 otherwise.
FFI_PLUGIN_EXPORT int isMixerCaptureRunning();

/// Get the pointer to the circular capture buffer.
FFI_PLUGIN_EXPORT unsigned char *getMixerCaptureBufferPointer();

/// Get the total size of the circular capture buffer in bytes.
FFI_PLUGIN_EXPORT uint64_t getMixerCaptureBufferSize();

/// Get the number of bytes available to read in the capture buffer.
FFI_PLUGIN_EXPORT uint64_t getMixerCaptureAvailableBytes();

/// Get the current read offset in the capture buffer.
FFI_PLUGIN_EXPORT uint64_t getMixerCaptureReadOffset();

/// Advance the read position by [bytes].
FFI_PLUGIN_EXPORT void advanceMixerCaptureReadPosition(uint64_t bytes);

/// Set the callback invoked when [notificationThresholdBytes] are available.
FFI_PLUGIN_EXPORT void setMixerOutputCallback(
    void (*callback)(unsigned char *, uint64_t));
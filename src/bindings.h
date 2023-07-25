#ifndef BINDINGS_H
#define BINDINGS_H

#include "enums.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

FFI_PLUGIN_EXPORT void test();

/// @brief Set a dart function to call when the sound with [handle] handle ends
/// @param callback this is the dart function that will be called
///     when the sound ends to play. 
///     Must be global or a static class member:
///     ```@pragma('vm:entry-point')
///        void playEndedCallback(int handle) {
///             // here the sound with [handle] has ended.
///             // you can play again
///             soLoudController.soLoudFFI.play(handle);
///             // or dispose it
///             soLoudController.soLoudFFI.stop(handle);
///        }
///     ```
/// @param handle the handle to the sound
/// @return callback this is the dart function that will be called
///         when the sound ends to play
/// @return true if success;
// FFI_PLUGIN_EXPORT bool setPlayEndedCallback
//     (void (*callback)(unsigned int), unsigned int handle);

/// @brief Initialize the player. Must be called before any other player functions
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT enum PlayerErrors initEngine();

/// @brief Must be called when there is no more need of the player or when closing the app
/// @return 
FFI_PLUGIN_EXPORT void dispose();

/// @brief Load a new sound to be played once or multiple times later
/// @param completeFileName the complete file path
/// @param hash return hash of the sound
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT enum PlayerErrors loadFile(char * completeFileName, unsigned int *hash);

/// @brief Speech
/// @param textToSpeech
/// @param handle sound identifier
/// @return Returns [PlayerErrors.noError] if success
/// TODO(me): add other T2S parameters
FFI_PLUGIN_EXPORT enum PlayerErrors speechText(
    char * textToSpeech, unsigned int *handle);

/// @brief Pause or unpause already loaded sound identified by [handle]
/// @param handle the sound handle
FFI_PLUGIN_EXPORT void pauseSwitch(unsigned int handle);

/// @brief Gets the pause state
/// @param handle the sound handle
/// @return true if paused
FFI_PLUGIN_EXPORT int getPause(unsigned int handle);

/// @brief Play already loaded sound identified by [handle]
/// @param hash the unique sound hash of a sound
/// @param volume 1.0f full volume
/// @param pan 0.0f centered
/// @param paused 0 not pause
/// @return the handle of the sound, 0 if error
FFI_PLUGIN_EXPORT unsigned int play(
    unsigned int hash,
    float volume,
    float pan,
    bool paused);

/// @brief Stop already loaded sound identified by [handle] and clear it
/// @param handle 
FFI_PLUGIN_EXPORT void stop(unsigned int handle);

/// @brief Stop all handles of the already loaded sound identified by [hash] and clear it
/// @param hash
FFI_PLUGIN_EXPORT void stopSound(unsigned int soundHash);

/// @brief Enable or disable visualization
/// @param enabled 
/// @return 
FFI_PLUGIN_EXPORT void setVisualizationEnabled(bool enabled);

/// @brief Returns valid data only if VisualizationEnabled is true
/// @param fft 
/// @return a 256 float array containing FFT data.
FFI_PLUGIN_EXPORT void getFft(float* fft);

/// @brief Returns valid data only if VisualizationEnabled is true
/// @param fft 
/// @return a 256 float array containing wave data.
FFI_PLUGIN_EXPORT void getWave(float* wave);

/// @brief return in [samples] a 512 float array. 
///     The first 256 floats represent the FFT frequencies data [0.0~1.0].
///     The other 256 floats represent the wave data (amplitude) [-1.0~1.0].
/// @param samples should be allocated and freed in dart side
/// @return 
FFI_PLUGIN_EXPORT void getAudioTexture(float* samples);

/// @brief Return a floats matrix of 256x512
/// Every row are composed of 256 FFT values plus 256 of wave data
/// Every time is called, a new row is stored in the
/// first row and all the previous rows are shifted
/// up (the last one will be lost).
/// @param samples 
/// @return 
FFI_PLUGIN_EXPORT enum PlayerErrors getAudioTexture2D(float** samples);

/// @brief get the sound length in seconds
/// @param soundHash the sound hash
/// @return returns sound length in seconds
FFI_PLUGIN_EXPORT double getLength(unsigned int soundHash);

/// @brief seek playing in [time] seconds
/// @param time
/// @param handle the sound handle
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT enum PlayerErrors seek(unsigned int handle,float time);

/// @brief get current sound position  in seconds
/// @param handle the sound handle
/// @return time in seconds
FFI_PLUGIN_EXPORT double getPosition(unsigned int handle);

/// @brief check if a handle is still valid.
/// @param handle handle to check
/// @return true if it still exists
FFI_PLUGIN_EXPORT int getIsValidVoiceHandle(unsigned int handle);

/// @brief smooth FFT data. 
/// When new data is read and the values are decreasing, the new value will be
/// decreased with an amplitude between the old and the new value.
/// This will result on a less shaky visualization
/// @param [smooth] must be in the [0.0 ~ 1.0] range.
/// 0 = no smooth
/// 1 = full smooth
/// the new value is calculated with:
/// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
/// @return 
FFI_PLUGIN_EXPORT void setFftSmoothing(float smooth);



#ifdef __cplusplus
}
#endif

#endif // BINDINGS_H
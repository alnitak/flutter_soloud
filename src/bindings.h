#ifndef BINDINGS_H
#define BINDINGS_H

#include "enums.h"
#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

FFI_PLUGIN_EXPORT void test();

/// @brief Initialize the player. Must be called before any other player functions
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT PlayerErrors initEngine();

/// @brief Must be called when there is no more need of the player or when closing the app
/// @return 
FFI_PLUGIN_EXPORT void dispose();

/// @brief Play a new file
/// @param completeFileName the complete file path
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT PlayerErrors playFile(char * completeFileName);

/// @brief Speech
/// @param textToSpeech
/// @return Returns [PlayerErrors.noError] if success
/// TODO(me): add other T2S parameters
FFI_PLUGIN_EXPORT PlayerErrors speechText(char * textToSpeech);

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
/// Every row are composed of 256 FFT values plus 256 wave data
/// Every time is called, a new row is stored in the
/// first row and all the previous rows are shifted
/// up (the last will be lost).
/// @param samples 
/// @return 
FFI_PLUGIN_EXPORT void getAudioTexture2D(float** samples);

/// @brief get the sound length in seconds
/// @return returns sound length in seconds
FFI_PLUGIN_EXPORT double getLength();

/// @brief seek playing in [time] seconds
/// @param [time] 
/// @return Returns [PlayerErrors.noError] if success
FFI_PLUGIN_EXPORT PlayerErrors seek(float time);

/// @brief get current sound position
/// @return time in seconds
FFI_PLUGIN_EXPORT float getPosition();

/// @brief smooth FFT data. 
/// When new data is read and the values are decreasing, the new value will be
/// decreased with an amplitude between the old and the new value.
/// This will resul on a less shaky visualization
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
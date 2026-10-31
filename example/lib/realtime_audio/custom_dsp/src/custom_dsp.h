#ifndef CUSTOM_DSP_H_
#define CUSTOM_DSP_H_

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #define CUSTOM_DSP_EXPORT __declspec(dllexport)
#else
  #define CUSTOM_DSP_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// 1. Custom Audio Source Callback matching `SoLoudSourceDspCallback_t`.
/// Generates procedural stereo dual-tone audio (Left: 220Hz, Right: 330Hz).
CUSTOM_DSP_EXPORT unsigned int myCustomSourceDspCallback(
    float *aBuffer,
    unsigned int aSamplesToRead,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    double aStreamTime,
    double aStreamPosition,
    uint32_t *aOutHasEnded,
    void *aUserData);

/// 2. Custom Filter Callback matching `SoLoudFilterDspCallback_t`.
/// Applies soft saturation/overdrive driven by user parameter 1 (Drive).
CUSTOM_DSP_EXPORT void myCustomFilterDspCallback(
    float *aBuffer,
    unsigned int aSamples,
    unsigned int aBufferSize,
    unsigned int aChannels,
    float aSamplerate,
    double aTime,
    const float *aParams,
    uint64_t aParamChangedMask,
    unsigned int aParamCount,
    void *aUserData);

/// Helper: returns the function pointer address of `myCustomSourceDspCallback`.
CUSTOM_DSP_EXPORT void *getCustomSourceCallbackAddress(void);

/// Helper: returns the function pointer address of `myCustomFilterDspCallback`.
CUSTOM_DSP_EXPORT void *getCustomFilterCallbackAddress(void);

#ifdef __cplusplus
}
#endif

#endif // CUSTOM_DSP_H_

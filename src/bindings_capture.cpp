#include "analyzer.h"
#include "capture.h"
#ifndef COMMON_H
#include "common.h"
#endif

#include <stdio.h>
#include <iostream>
#include <memory.h>
#include <memory>

#ifdef __cplusplus
extern "C" {
#endif

Capture capture;
std::unique_ptr<Analyzer> analyzerCapture = std::make_unique<Analyzer>(256);

FFI_PLUGIN_EXPORT enum CaptureErrors initCapture()
{
    CaptureErrors res = (CaptureErrors)capture.init();
    if (res != noError) return res;
    return capture_noError;
}

FFI_PLUGIN_EXPORT void disposeCapture()
{
    capture.dispose();
}

FFI_PLUGIN_EXPORT int isCaptureInited()
{
    return capture.isInited() ? 1 : 0;
}

FFI_PLUGIN_EXPORT enum CaptureErrors startCapture()
{
    if (!capture.isInited()) return capture_not_inited;

    return capture.startCapture();
}

FFI_PLUGIN_EXPORT enum CaptureErrors stopCapture()
{
    if (!capture.isInited()) return capture_not_inited;

    return capture.stopCapture();
}


FFI_PLUGIN_EXPORT void getCaptureTexture(float* samples)
{
    if (analyzerCapture.get() == nullptr) {
        memset(samples,0, sizeof(float) * 512);
        return;
    }
    float *wave = capture.getWave();
    float *fft = analyzerCapture.get()->calcFFT(wave);

    memcpy(samples, fft, sizeof(float) * 256);
    memcpy(samples + 256, wave, sizeof(float) * 256);
}

float capturedTexture2D[256][512];
FFI_PLUGIN_EXPORT enum CaptureErrors getCaptureAudioTexture2D(float** samples)
{
    if (analyzerCapture.get() == nullptr || !capture.isInited()) {
        memset(samples,0, sizeof(float) * 512 * 256);
        return capture_not_inited;
    }
    /// shift up 1 row
    memmove(*capturedTexture2D+512, capturedTexture2D, sizeof(float) * 512 * 255);
    /// store the new 1st row
    getCaptureTexture(capturedTexture2D[0]);
    *samples = *capturedTexture2D;
    return capture_noError;
}

FFI_PLUGIN_EXPORT enum CaptureErrors setCaptureFftSmoothing(float smooth)
{
    if (!capture.isInited()) return capture_not_inited;
    analyzerCapture.get()->setSmoothing(smooth);
}

#ifdef __cplusplus
}
#endif
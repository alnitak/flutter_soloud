#include "analyzer.h"
#include "capture.h"
#ifndef COMMON_H
#include "common.h"
#endif

#include <stdio.h>
#include <vector>
#include <iostream>
#include <memory.h>
#include <memory>

#ifdef __cplusplus
extern "C"
{
#endif

    Capture capture;
    std::unique_ptr<Analyzer> analyzerCapture = std::make_unique<Analyzer>(256);

    FFI_PLUGIN_EXPORT void listCaptureDevices(
        char **devicesName,
        int **isDefault,
        int *n_devices)
    {
        std::vector<CaptureDevice> d = capture.listCaptureDevices();

        int numDevices = 0;
        for (int i = 0; i < (int)d.size(); i++)
        {
            bool hasSpecialChar = false;
            /// check if the device name has some strange chars (happens on linux)
            for (int n = 0; n < 5; n++)
                if (d[i].name[n] < 0x20)
                    hasSpecialChar = true;
            if (strlen(d[i].name) <= 5 || hasSpecialChar)
                continue;

            devicesName[i] = strdup(d[i].name);
            isDefault[i] = (int *)malloc(sizeof(int *));
            *isDefault[i] = d[i].isDefault;

            numDevices++;
        }
        *n_devices = numDevices;
    }

    FFI_PLUGIN_EXPORT void freeListCaptureDevices(
        char **devicesName,
        int **isDefault,
        int n_devices)
    {
        for (int i = 0; i < n_devices; i++)
        {
            free(devicesName[i]);
            free(isDefault[i]);
        }
    }

    FFI_PLUGIN_EXPORT enum CaptureErrors initCapture(int deviceID)
    {
        CaptureErrors res = capture.init(deviceID);
        return res;
    }

    FFI_PLUGIN_EXPORT void disposeCapture()
    {
        capture.dispose();
    }

    FFI_PLUGIN_EXPORT int isCaptureInited()
    {
        return capture.isInited() ? 1 : 0;
    }

    FFI_PLUGIN_EXPORT int isCaptureStarted()
    {
        return capture.isStarted() ? 1 : 0;
    }

    FFI_PLUGIN_EXPORT enum CaptureErrors startCapture()
    {
        return capture.startCapture();
    }

    FFI_PLUGIN_EXPORT enum CaptureErrors stopCapture()
    {
        return capture.stopCapture();
    }

    /// Return a 256 float array containing FFT data.
    FFI_PLUGIN_EXPORT void getCaptureFft(float **fft)
    {
        if (!capture.isInited())
            return;
        float *wave = capture.getWave();
        *fft = analyzerCapture.get()->calcFFT(wave);
    }

    /// Return a 256 float array containing wave data.
    FFI_PLUGIN_EXPORT void getCaptureWave(float **wave)
    {
        if (!capture.isInited())
            return;
        *wave = capture.getWave();
    }

    FFI_PLUGIN_EXPORT void getCaptureTexture(float *samples)
    {
        if (analyzerCapture.get() == nullptr || !capture.isInited())
        {
            memset(samples, 0, sizeof(float) * 512);
            return;
        }
        float *wave = capture.getWave();
        float *fft = analyzerCapture.get()->calcFFT(wave);

        memcpy(samples, fft, sizeof(float) * 256);
        memcpy(samples + 256, wave, sizeof(float) * 256);
    }

    float capturedTexture2D[256][512];
    FFI_PLUGIN_EXPORT enum CaptureErrors getCaptureAudioTexture2D(float **samples)
    {
        if (analyzerCapture.get() == nullptr || !capture.isInited())
        {
            *samples = *capturedTexture2D;
            memset(*samples, 0, sizeof(float) * 512 * 256);
            return capture_not_inited;
        }
        /// shift up 1 row
        memmove(*capturedTexture2D + 512, capturedTexture2D, sizeof(float) * 512 * 255);
        /// store the new 1st row
        getCaptureTexture(capturedTexture2D[0]);
        *samples = *capturedTexture2D;
        return capture_noError;
    }

    FFI_PLUGIN_EXPORT float getCaptureTextureValue(int row, int column) {
        return capturedTexture2D[row][column];
    }

    FFI_PLUGIN_EXPORT enum CaptureErrors setCaptureFftSmoothing(float smooth)
    {
        if (!capture.isInited())
            return capture_not_inited;
        analyzerCapture.get()->setSmoothing(smooth);
        return capture_noError;
    }

#ifdef __cplusplus
}
#endif
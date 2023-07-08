#include "bindings.h"
#include "player.h"
#include "analyzer.h"
#ifndef COMMON_H
#include "common.h"
#endif

#include "soloud/include/soloud_fft.h"
#include "soloud_thread.h"

#include <stdio.h>
#include <iostream>
#include <memory.h>
#include <memory>


#ifdef __cplusplus
extern "C" {
#endif

Player player;
std::unique_ptr<Analyzer> analyzer = std::make_unique<Analyzer>(2048);

FFI_PLUGIN_EXPORT PlayerErrors initEngine()
{
    PlayerErrors res = (PlayerErrors)player.init();
    if (res != noError) return res;

    const int windowSize = (
            player.soloud.getBackendBufferSize() / 
            player.soloud.getBackendChannels()
        ) - 1;
    analyzer.get()->setWindowsSize(windowSize);
    return noError;
}

FFI_PLUGIN_EXPORT void dispose()
{
    player.dispose();
}

FFI_PLUGIN_EXPORT PlayerErrors playFile(char * completeFileName)
{
    return (PlayerErrors)player.play(completeFileName);
}

FFI_PLUGIN_EXPORT PlayerErrors speechText(char * textToSpeech)
{
    return (PlayerErrors)player.textToSpeech(textToSpeech);
}

FFI_PLUGIN_EXPORT void setVisualizationEnabled(bool enabled)
{
    if (enabled) {
        SoLoud::Thread::sleep(100);
        player.setVisualizationEnabled(true);
    } else {
        player.setVisualizationEnabled(false);
        analyzer.reset();
    }
}

FFI_PLUGIN_EXPORT void getFft(float* fft)
{
    fft = player.calcFFT();
}

FFI_PLUGIN_EXPORT void getWave(float* wave)
{
    wave = player.getWave();
}

FFI_PLUGIN_EXPORT void setFftSmoothing(float smooth)
{
    analyzer.get()->setSmoothing(smooth);
}

FFI_PLUGIN_EXPORT void getAudioTexture(float* samples)
{
    if (analyzer.get() == nullptr) {
        memset(samples,0, sizeof(float) * 512);
        return;
    }
    float *wave = player.getWave();
    float *fft = analyzer.get()->calcFFT(wave);

    memcpy(samples, fft, sizeof(float) * 256);
    memcpy(samples + 256, wave, sizeof(float) * 256);
}

float texture2D[256][512];
FFI_PLUGIN_EXPORT void getAudioTexture2D(float** samples)
{
    if (analyzer.get() == nullptr) {
        memset(samples,0, sizeof(float) * 512 * 256);
        return;
    }
    /// shift up 1 row
    memmove(*texture2D+512, texture2D, sizeof(float) * 512 * 255);
    /// store the new 1st row
    getAudioTexture(texture2D[0]);
    *samples = *texture2D;
}

FFI_PLUGIN_EXPORT double getLength()
{
    return player.getLength();
}

FFI_PLUGIN_EXPORT PlayerErrors seek(float time)
{
    return (PlayerErrors)player.seek(time);
}

FFI_PLUGIN_EXPORT float getPosition()
{
    return (float)player.getPosition();
}




FFI_PLUGIN_EXPORT void test()
{

}


#ifdef __cplusplus
}
#endif
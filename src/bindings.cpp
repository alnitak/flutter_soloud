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

FFI_PLUGIN_EXPORT enum PlayerErrors initEngine()
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

FFI_PLUGIN_EXPORT enum PlayerErrors playFile(char * completeFileName, unsigned int *handle)
{
    if (!player.isInited()) return backendNotInited;
    return (PlayerErrors)player.play(completeFileName, *handle);
}

FFI_PLUGIN_EXPORT enum PlayerErrors speechText(char * textToSpeech, unsigned int *handle)
{
    if (!player.isInited()) return backendNotInited;
    return (PlayerErrors)player.textToSpeech(textToSpeech, *handle);
}

/// @brief Pause already loaded sound identified by [handle]
/// @param handle 
FFI_PLUGIN_EXPORT void pause(unsigned int handle)
{
    if (!player.isInited()) return;
    player.pause(handle);
}

/// @brief Play already loaded sound identified by [handle]
/// @param handle 
FFI_PLUGIN_EXPORT void play(unsigned int handle)
{
    if (!player.isInited()) return;
    player.play(handle);
}

/// @brief Stop already loaded sound identified by [handle] and clear it
/// @param handle 
FFI_PLUGIN_EXPORT void stop(unsigned int handle)
{
    if (!player.isInited()) return;
    player.stop(handle);
}


FFI_PLUGIN_EXPORT void setVisualizationEnabled(bool enabled)
{
    if (!player.isInited()) return;
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
    if (!player.isInited()) return;
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

FFI_PLUGIN_EXPORT double getLength(unsigned int handle)
{
    if (!player.isInited()) return 0.0;
    return player.getLength(handle);
}

FFI_PLUGIN_EXPORT enum PlayerErrors seek(unsigned int handle, float time)
{
    if (!player.isInited()) return backendNotInited;
    return (PlayerErrors)player.seek(handle, time);
}

FFI_PLUGIN_EXPORT double getPosition(unsigned int handle)
{
    if (!player.isInited() || player.getSoundsCount() == 0) return 0.0f;
    return player.getPosition(handle);
}






    SoLoud::Wav sound1;
    SoLoud::Wav sound2;
    SoLoud::Soloud soloud;

FFI_PLUGIN_EXPORT void test()
{
    // unsigned int handle;
    // SoLoud::result result = soloud.init(
    //     SoLoud::Soloud::CLIP_ROUNDOFF, 
    //     SoLoud::Soloud::MINIAUDIO, 44100, 2048, 2U);
    // result = sound1.load("/home/deimos/5/01 - Theme From Farscape.mp3");
    // result = sound2.load("/home/deimos/5/Music/ROSS/DANCE/Alphaville - Big In Japan (Original Version).mp3");

    // soloud.play(sound1, -1.0f, 0.0f, 0, 0);
    // soloud.play(sound2, -1.0f, 0.0f, 0, 0);

    // unsigned int handle;
    // player.play("/home/deimos/5/01 - Theme From Farscape.mp3", handle);
    // player.play("/home/deimos/5/Music/ROSS/DANCE/Alphaville - Big In Japan (Original Version).mp3", handle);

    player.debug();
}


#ifdef __cplusplus
}
#endif
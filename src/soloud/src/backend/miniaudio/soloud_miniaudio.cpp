/*
SoLoud audio engine
Copyright (c) 2013-2020 Jari Komppa

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
claim that you wrote the original software. If you use this software
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original software.

3. This notice may not be removed or altered from any source
distribution.
*/
#include <stdlib.h>

#include "soloud.h"

#if !defined(WITH_MINIAUDIO)

namespace SoLoud
{
    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer)
    {
        return NOT_IMPLEMENTED;
    }
}

#else

#define MINIAUDIO_IMPLEMENTATION
// // #define MA_NO_NULL
// #define MA_NO_DECODING
// #define MA_NO_WAV
// #define MA_NO_FLAC
// #define MA_NO_MP3
// #define MA_NO_AUTOINITIALIZATION
// #define MA_NO_VORBIS
// #define MA_NO_OPUS
#define MA_NO_MIDI

// Seems that on miniaudio there is still an issue when uninitializing the device
// addressed by this issue: https://github.com/mackron/miniaudio/issues/466
// For me this happens using AAudio on android <= 10 (but not on Samsung Galaxy S9+).
// Disablig AAudio in favor of OpenSL is a workaround to prevent the crash.
#if defined(__ANDROID__) && (__ANDROID_API__ <= 29)
#define MA_NO_AAUDIO
#endif
// #define MA_DEBUG_OUTPUT
#include "miniaudio.h"
#include <math.h>

namespace SoLoud
{
    ma_device gDevice;
    SoLoud::Soloud *soloud;
    ma_context context;

    // Added by Marco Bavagnoli
    void on_notification(const ma_device_notification* pNotification)
    {
        MA_ASSERT(pNotification != NULL);
        if (soloud->_stateChangedCallback == nullptr)
            return;

        switch (pNotification->type)
        {
            case ma_device_notification_type_started:
            {
                soloud->_stateChangedCallback(0);
            }
            break;

            case ma_device_notification_type_stopped:
            {
                soloud->_stateChangedCallback(1);
            } break;

            case ma_device_notification_type_rerouted:
            {
                soloud->_stateChangedCallback(2);
            } break;

            case ma_device_notification_type_interruption_began:
            {
                soloud->_stateChangedCallback(3);
            } break;

            case ma_device_notification_type_interruption_ended:
            {
#if defined(MA_HAS_COREAUDIO)
                // On macOS and iOS when the the interruption begins
                // the device is automatically stopped (not uninited with ma_device_uninit).
                // So we need to start it again when the interruption ends.
                miniaudio_ensureDeviceStarted_impl();
#endif
                soloud->_stateChangedCallback(4);
            } break;

            case ma_device_notification_type_unlocked:
            {
                soloud->_stateChangedCallback(5);
            } break;

            default: break;
        }
    }

    void soloud_miniaudio_audiomixer(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        SoLoud::Soloud *soloud = (SoLoud::Soloud *)pDevice->pUserData;
        soloud->mix((float *)pOutput, frameCount);
    }

    static void soloud_miniaudio_deinit(SoLoud::Soloud *aSoloud)
    {
        ma_device_stop(&gDevice);
        ma_device_uninit(&gDevice);
#if defined(MA_HAS_COREAUDIO)
        ma_context_uninit(&context);
#endif
    }

    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels, void *pPlaybackInfos_id)
    {
        soloud = aSoloud;
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        if (pPlaybackInfos_id != NULL)
        {
            deviceConfig.playback.pDeviceID = (ma_device_id*)pPlaybackInfos_id;
        }
        deviceConfig.periodSizeInFrames = aBuffer;
        deviceConfig.playback.format    = ma_format_f32;
        deviceConfig.playback.channels  = aChannels;
        deviceConfig.sampleRate         = aSamplerate;
        deviceConfig.dataCallback       = soloud_miniaudio_audiomixer;
        deviceConfig.pUserData          = (void *)aSoloud;

        // deviceConfig.aaudio.usage       = ma_aaudio_usage_default;
        // deviceConfig.aaudio.contentType = ma_aaudio_content_type_default;
        // deviceConfig.aaudio.inputPreset = ma_aaudio_input_preset_default;
        if (aSoloud->_stateChangedCallback != nullptr)
            deviceConfig.notificationCallback = on_notification;

#if defined(MA_HAS_COREAUDIO)
        // Disable CoreAudio context
        ma_context_config contextConfig = ma_context_config_init();
        contextConfig.coreaudio.sessionCategory = ma_ios_session_category_none;
        contextConfig.coreaudio.noAudioSessionActivate = true;
        contextConfig.coreaudio.noAudioSessionDeactivate = true;

        ma_result result = ma_context_init(NULL, 0, &contextConfig, &context);
        if (result != MA_SUCCESS) {
            return UNKNOWN_ERROR;
        }
        if (ma_device_init(&context, &deviceConfig, &gDevice) != MA_SUCCESS)
        {
            ma_context_uninit(&context);
            return UNKNOWN_ERROR;
        }
#else
        if (ma_device_init(NULL, &deviceConfig, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }
#endif


        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;

        ma_device_start(&gDevice);
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }

    result miniaudio_changeDevice_impl(void *pPlaybackInfos_id)
    {
        if (soloud == nullptr)
            return UNKNOWN_ERROR;

        ma_device_uninit(&gDevice);
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.pDeviceID = (ma_device_id *)pPlaybackInfos_id;
        deviceConfig.periodSizeInFrames = soloud->mBufferSize;
        deviceConfig.playback.format    = ma_format_f32;
        deviceConfig.playback.channels  = soloud->mChannels;
        deviceConfig.sampleRate         = soloud->mSamplerate;
        deviceConfig.dataCallback       = soloud_miniaudio_audiomixer;
        deviceConfig.pUserData          = (void *)soloud;
        if (ma_device_init(NULL, &deviceConfig, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }
        ma_device_start(&gDevice);
        return 0;
    }

    // Added to ensure miniaudio device is started when needed, ie by an interruption.
    // On macOS and iOS when an interruption begins (ie anothe app needs the audio context),
    // the device is automatically stopped (not uninited with ma_device_uninit).
    // So we need to check if the device is stopped and start it again.
    result miniaudio_ensureDeviceStarted_impl()
    {
        if (soloud == nullptr)
            return UNKNOWN_ERROR;

        // Check if device is stopped and start it if needed
        if (ma_device_get_state(&gDevice) == ma_device_state_stopped)
        {
            ma_result result = ma_device_start(&gDevice);
            if (result != MA_SUCCESS)
            {
                return UNKNOWN_ERROR;
            }
        }
        return 0;
    }
};
#endif

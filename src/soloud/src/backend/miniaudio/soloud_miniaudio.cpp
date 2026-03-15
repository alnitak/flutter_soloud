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
// #if defined(__ANDROID__) && (__ANDROID_API__ <= 29)
// #define MA_NO_AAUDIO
// #endif
// #define MA_DEBUG_OUTPUT
#include "miniaudio.h"
#ifdef __ANDROID__
#include <android/api-level.h>
#endif
#include <math.h>

namespace SoLoud
{
    ma_device gDevice;
    SoLoud::Soloud *soloud;
    ma_context context;

    // Forward declarations for functions used in on_notification
    result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud);
    result soloud_miniaudio_resume(SoLoud::Soloud *aSoloud);

    // Added by Marco Bavagnoli
    void on_notification(const ma_device_notification* pNotification)
    {
        MA_ASSERT(pNotification != NULL);

        switch (pNotification->type)
        {
            case ma_device_notification_type_started:
            {
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(0);
            } break;

            case ma_device_notification_type_stopped:
            {
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(1);
            } break;

            case ma_device_notification_type_rerouted:
            {
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(2);
            } break;

            case ma_device_notification_type_interruption_began:
            {
                // Automatically pause the audio device when the OS signals an interruption.
                // This ensures the audio device is properly paused without requiring the
                // developer to manually call pauseAudioDevice().
                soloud_miniaudio_pause(soloud);
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(3);
            } break;

            case ma_device_notification_type_interruption_ended:
            {
                // On CoreAudio platforms (macOS/iOS) when the the interruption begins
                // the device is automatically stopped (not uninited with ma_device_uninit).
                // So we need to start it again when the interruption ends.
                soloud->resume();
                if (soloud->_stateChangedCallback != nullptr)
                    soloud->_stateChangedCallback(4);
            } break;

            case ma_device_notification_type_unlocked:
            {
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(5);
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
#if defined(MA_HAS_COREAUDIO) || defined(__ANDROID__)
        ma_context_uninit(&context);
#endif
    }

    // Pause the audio device: stops the CoreAudio AudioUnit (or platform equivalent)
    // without uninitialising it. This is the correct way to "pause" on iOS/macOS —
    // it tells the OS the app has nothing to render, which preserves AVAudioSession
    // state and keeps MPRemoteCommandCenter routing intact.
    result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud)
    {
        if (ma_device_get_state(&gDevice) == ma_device_state_started)
        {
            ma_result res = ma_device_stop(&gDevice);
            if (res != MA_SUCCESS)
                return UNKNOWN_ERROR;
        }
        return 0;
    }

    // Resume the audio device after soloud_miniaudio_pause(). On iOS, the
    // AVAudioSession must already be active (the app is responsible for calling
    // [AVAudioSession setActive:YES]) before calling this.
    result soloud_miniaudio_resume(SoLoud::Soloud *aSoloud)
    {
        if (aSoloud == nullptr)
            return UNKNOWN_ERROR;

        // Check if device is stopped and start it if needed
        if (ma_device_get_state(&gDevice) == ma_device_state_stopped)
        {
#if defined(MA_APPLE_MOBILE)
            // On iOS, after any audio interruption the AVAudioSession MUST be
            // explicitly re-activated before restarting the Audio Unit.
            //
            // Without this call, iOS does not restore remote command routing
            // (Lock Screen controls, AirPods) to this app after the device
            // restarts. This is because:
            //   1. miniaudio registers its own AVAudioSessionInterruptionNotification
            //      observer alongside audio_session (the Flutter package), so both
            //      handle interruptions concurrently.
            //   2. miniaudio can restart AudioOutputUnit before audio_session has
            //      had a chance to call setActive:YES, leaving the unit running
            //      against an inactive session — breaking remote command routing.
            //   3. Apple's audio interruption recovery guidelines explicitly require
            //      setActive:YES before restarting the Audio Unit.
            @autoreleasepool {
                [[AVAudioSession sharedInstance] setActive:YES error:nil];
            }
#endif
            ma_result result = ma_device_start(&gDevice);
            if (result != MA_SUCCESS)
                return UNKNOWN_ERROR;
        }
        return 0;
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
#elif defined(__ANDROID__)
        ma_backend backends[] = { ma_backend_aaudio, ma_backend_opensl };
        ma_uint32 backendCount = 2;
        if (android_get_device_api_level() <= 29) {
            backends[0] = ma_backend_opensl;
            backendCount = 1;
        }

        ma_context_config contextConfig = ma_context_config_init();
        if (ma_context_init(backends, backendCount, &contextConfig, &context) != MA_SUCCESS) {
            return UNKNOWN_ERROR;
        }
        if (ma_device_init(&context, &deviceConfig, &gDevice) != MA_SUCCESS) {
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
        aSoloud->mBackendPauseFunc   = soloud_miniaudio_pause;
        aSoloud->mBackendResumeFunc  = soloud_miniaudio_resume;

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
};
#endif

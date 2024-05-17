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
// #define MA_NO_NULL
#define MA_NO_DECODING
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_AUTOINITIALIZATION
#define MA_NO_VORBIS
#define MA_NO_OPUS
#define MA_NO_MIDI

// Seems that on miniaudio there is still an issue when uninitializing the device
// addressed by this issue: https://github.com/mackron/miniaudio/issues/466
// For me this happens using AAudio on android <= 10 (but not on Samsung Galaxy S9+).
// Disablig AAudio in favor of OpenSL is a workaround to prevent the crash.
#if defined(__ANDROID__) && (__ANDROID_API__ <= 29)
#define MA_NO_AAUDIO
#endif
// #define MA_DEBUG_OUTPUT
// #define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include <math.h>

namespace SoLoud
{
    ma_device gDevice;

    void on_notification(const ma_device_notification* pNotification)
    {
        MA_ASSERT(pNotification != NULL);
        
        switch (pNotification->type)
        {
            case ma_device_notification_type_started:
            {
                printf("Started\n");
            } break;

            case ma_device_notification_type_stopped:
            {
                printf("Stopped\n");
            } break;

            case ma_device_notification_type_rerouted:
            {
                printf("Rerouted\n");
            } break;

            case ma_device_notification_type_interruption_began:
            {
                printf("Interruption Began\n");
            } break;

            case ma_device_notification_type_interruption_ended:
            {
                printf("Interruption Ended\n");
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
        ma_device_uninit(&gDevice);
    }

    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels)
    {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.periodSizeInFrames = aBuffer;
        config.playback.format    = ma_format_f32;
        config.playback.channels  = aChannels;
        config.sampleRate         = aSamplerate;
        config.dataCallback       = soloud_miniaudio_audiomixer;
        config.pUserData          = (void *)aSoloud;
        config.notificationCallback = on_notification;

        if (ma_device_init(NULL, &config, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }

        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;

        ma_device_start(&gDevice);
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }
};
#endif

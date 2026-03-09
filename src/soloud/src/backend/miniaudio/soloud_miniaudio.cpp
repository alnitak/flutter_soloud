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
#include <thread>
#include <mutex>
#include <chrono>

namespace SoLoud
{
    ma_device gDevice;
    SoLoud::Soloud *soloud;
    ma_context context;
    static bool gDeviceStartDeferred = false; // Track deferred device start on Windows
    static bool gDeviceInitDeferred = false;  // Track deferred device init on Windows
    static bool gDeviceInitialized = false;   // Track if device is actually initialized
    static std::thread* gInitThread = nullptr; // Background thread for device init
    static std::mutex gInitMutex; // Protect device init state
    
    // Configuration to store for deferred initialization
    struct DeferredDeviceConfig {
        ma_device_config config;
        ma_context_config contextConfig;
        bool useContext;
        bool useContextConfig;
    };
    static DeferredDeviceConfig gDeferredConfig;

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
        // Clean up initialization thread if it's still running
        if (gInitThread != nullptr)
        {
            if (gInitThread->joinable())
            {
                gInitThread->join();
            }
            delete gInitThread;
            gInitThread = nullptr;
        }
        
        if (gDeviceInitialized)
        {
            // From miniaudio.h doc:
            // "This will explicitly stop the device. You do not need to call `ma_device_stop()` beforehand, but it's harmless if you do."
            // But probably by adding the initialization thread, the call to ma_device_stop() was causing the #413 issue (hang on exit app).
            ma_device_uninit(&gDevice);
            gDeviceInitialized = false;
        }
#if defined(MA_HAS_COREAUDIO) || defined(__ANDROID__)
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

        if (aSoloud->_stateChangedCallback != nullptr)
            deviceConfig.notificationCallback = on_notification;

#ifdef _WIN32
        // On Windows, defer the entire device initialization to avoid interfering with
        // the main thread's message pump. This fixes compatibility with plugins like
        // desktop_drop that rely on COM windowed messages.
        gDeferredConfig.config = deviceConfig;
        gDeferredConfig.useContext = false;
        gDeferredConfig.useContextConfig = false;
        gDeviceInitDeferred = true;
        gDeviceStartDeferred = false;
        
        // Use safe default values for postinit
        aSoloud->postinit_internal(aSamplerate, aBuffer, aFlags, aChannels);
        
#elif defined(MA_HAS_COREAUDIO)
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
        gDeviceInitialized = true;
        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);
        ma_device_start(&gDevice);
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
        
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
        gDeviceInitialized = true;
        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);
        ma_device_start(&gDevice);
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
        
#else
        // Linux and other platforms
        if (ma_device_init(NULL, &deviceConfig, &gDevice) != MA_SUCCESS)
        {
            return UNKNOWN_ERROR;
        }
        gDeviceInitialized = true;
        aSoloud->postinit_internal(gDevice.sampleRate, gDevice.playback.internalPeriodSizeInFrames, aFlags, gDevice.playback.channels);
        ma_device_start(&gDevice);
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
#endif

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }

    // Background thread function to initialize the audio device
    static void miniaudio_init_thread_func()
    {
        std::lock_guard<std::mutex> lock(gInitMutex);
        
        if (!gDeviceInitDeferred)
            return;

        if (ma_device_init(NULL, &gDeferredConfig.config, &gDevice) == MA_SUCCESS)
        {
            gDeviceInitDeferred = false;
            gDeviceInitialized = true;
            // Start the device after initialization
            if (ma_device_get_state(&gDevice) != ma_device_state_started)
            {
                ma_device_start(&gDevice);
            }
            gDeviceStartDeferred = false;
        }
    }

    // Ensure the device is started. Called on first audio operation on Windows.
    // On Windows, this runs device init on a background thread to avoid blocking the message pump.
    result miniaudio_ensure_device_started()
    {
        if (!gDeviceInitDeferred)
            return 0; // Already initialized and started

        // Create a background thread to initialize and start the device
        // This prevents the main thread's message pump from being blocked
        if (gInitThread == nullptr)
        {
            gInitThread = new std::thread(miniaudio_init_thread_func);
            
            // Wait for the thread to complete (with reasonable timeout)
            // The thread uses a mutex to protect device access
            if (gInitThread && gInitThread->joinable())
            {
                gInitThread->join();
                delete gInitThread;
                gInitThread = nullptr;
            }
        }

        // Verify the device is ready
        if (gDeviceInitDeferred)
            return UNKNOWN_ERROR; // Init failed
            
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
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
        gDeviceInitialized = true;
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

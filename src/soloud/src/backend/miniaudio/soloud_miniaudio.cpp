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
#include "soloud_internal.h"

#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#endif

namespace SoLoud
{
    // Selects the miniaudio performance profile used when (re)initializing the
    // device. Low-latency (the historical default) maps to AAudio's
    // PERFORMANCE_MODE_LOW_LATENCY / MMAP path on Android; that path can't be
    // captured by system screen recorders and leaves little callback headroom
    // for CPU-heavy filters. When this is false, the conservative (legacy
    // mixer) profile is used instead. Defined outside the WITH_MINIAUDIO guard
    // so the setter symbol always exists for the C bindings to call.
    static bool gMiniaudioLowLatency = true;

    void miniaudio_setLowLatency(bool aLowLatency)
    {
        gMiniaudioLowLatency = aLowLatency;
    }
}

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
#include <chrono>
#include <thread>
#include <mutex>
#include "soloud_common.h"
#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sys/resource.h>
#endif

namespace SoLoud
{
    ma_device gDevice;
    SoLoud::Soloud *soloud;
    ma_context context;
    volatile bool gDeviceStopped = true;  // Track device stopped state for proper cleanup

    // Forward declarations for functions used in on_notification
    result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud);
    result soloud_miniaudio_resume(SoLoud::Soloud *aSoloud);
    result miniaudio_ensure_thread_device_started();
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

        // Guard against notifications delivered after deinitialization.
        // The notifications may be pending on the main thread when the
        // device is torn down.
        if (soloud == nullptr)
            return;

        switch (pNotification->type)
        {
            case ma_device_notification_type_started:
            {
                gDeviceStopped = false;
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(0);
            } break;

            case ma_device_notification_type_stopped:
            {
                gDeviceStopped = true;
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(1);
            } break;

            case ma_device_notification_type_rerouted:
            {
                if (soloud->_stateChangedCallback != nullptr) soloud->_stateChangedCallback(2);
            } break;

            case ma_device_notification_type_interruption_began:
            {
                // Automatically pause the audio device when the OS signals an interruption.
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
        static bool first_call = true;
        if (first_call) {
#ifdef __ANDROID__
            int policy;
            struct sched_param param;
            if (pthread_getschedparam(pthread_self(), &policy, &param) == 0) {
                // Attempt to elevate to Realtime FIFO
                param.sched_priority = 1;
                if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
                    if (pthread_setschedparam(pthread_self(), SCHED_RR, &param) != 0) {
                        // If denied Realtime, check if we are stuck in SCHED_BATCH (3)
                        // and try to escape to SCHED_OTHER (0)
                        if (policy == 3) {
                             param.sched_priority = 0;
                             pthread_setschedparam(pthread_self(), 0, &param);
                        }
                    }
                }
                // Plus set highest niceness
                setpriority(PRIO_PROCESS, 0, -20);
            }
#endif
        }
        first_call = false;
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

        // Clear the global soloud pointer BEFORE uninitializing the device.
        // This prevents any pending platform notifications (e.g. iOS route
        // changes delivered on the main thread) from dereferencing a
        // destroyed SoLoud instance through the on_notification callback.
        soloud = nullptr;

        if (gDeviceInitialized)
        {
            // Check if device is already stopped before calling ma_device_stop()
            // (which can cause an ANR on Android using OpenSSL #333).
            // This should prevent ANR on Android where ma_device_stop() can block indefinitely
            // if the device is in an unknown state
            if (ma_device_get_state(&gDevice) != ma_device_state_stopped)
            {
                ma_device_stop(&gDevice);
                
                // Wait for device to actually stop before uninitializing
                // Timeout after 500ms to prevent infinite blocking
                int timeoutMs = 0;
                int maxTimeoutMs = 500;
                while (!gDeviceStopped && timeoutMs < maxTimeoutMs)
                {
                    // Small sleep to avoid busy-waiting
#if defined(_WIN32) || defined(_WIN64)
                    Sleep(1);
#else
                    usleep(1000);  // 1ms sleep
#endif
                    timeoutMs += 1;
                }
            }
            
            // Set flag to stopped in case notification wasn't received
            gDeviceStopped = true;
            
            // From miniaudio.h doc:
            // "This will explicitly stop the device. You do not need to call `ma_device_stop()` beforehand, but it's harmless if you do."
            ma_device_uninit(&gDevice);
            gDeviceInitialized = false;
        }
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
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
            /* On Web and Android, don't suspend the audio device to avoid a bug where
               stale buffered audio data can fire after the device is stopped but before
               it takes effect. When stop() and play() are called in quick succession,
               those stale buffers get queued and play after resume(), causing audio
               glitches and lag. Keeping the device running is safe: soloud->mix()
               produces silence when no voices are active, which has negligible overhead.
               This solves #446 on both Web and Android. */
            (void)aSoloud;
            return 0;
#else
            ma_result res = ma_device_stop(&gDevice);
            if (res != MA_SUCCESS)
                return UNKNOWN_ERROR;
#endif
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

        // Honor the requested performance profile (see gMiniaudioLowLatency).
        // Conservative keeps Android off the un-capturable MMAP path and gives
        // heavy DSP more headroom; the trade-off is higher output latency.
        deviceConfig.performanceProfile = gMiniaudioLowLatency
            ? ma_performance_profile_low_latency
            : ma_performance_profile_conservative;

#ifdef _WIN32
        // On Windows, defer the entire device initialization to avoid interfering with
        // the main thread's message pump. This fixes compatibility with plugins like
        // desktop_drop that rely on COM windowed messages.
        gDeferredConfig.config = deviceConfig;
        gDeferredConfig.useContext = false;
        gDeferredConfig.useContextConfig = false;
        gDeviceInitDeferred = true;
        gDeviceStartDeferred = false;
        
        // On Windows, start the audio device initialization in background.
        // This ensures the device is ready by the time play() is called,
        // without blocking the main thread's message pump.
        aSoloud->postinit_internal(aSamplerate, aBuffer, aFlags, aChannels);

        // Use safe default values for postinit
        miniaudio_ensure_thread_device_started();

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
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            ma_context_uninit(&context);
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
        
#elif defined(__ANDROID__)
        // When low-latency is disabled the device runs on the legacy mixer path
        // (set above). Also tag the AAudio stream as media/music and explicitly
        // allow capture so system screen recorders pick up the audio.
        if (!gMiniaudioLowLatency)
        {
            deviceConfig.aaudio.usage                = ma_aaudio_usage_media;
            deviceConfig.aaudio.contentType          = ma_aaudio_content_type_music;
            deviceConfig.aaudio.allowedCapturePolicy = ma_aaudio_allow_capture_by_all;
        }

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
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            ma_context_uninit(&context);
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
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
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
#endif

        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;
        aSoloud->mBackendPauseFunc   = soloud_miniaudio_pause;
        aSoloud->mBackendResumeFunc  = soloud_miniaudio_resume;
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
            gDeviceInitialized = true;
            // Start the device after initialization
            if (ma_device_get_state(&gDevice) != ma_device_state_started)
            {
                ma_result startResult = ma_device_start(&gDevice);
                if (startResult != MA_SUCCESS) {
                    soloud_platform_log("miniaudio_init_thread_func: ma_device_start failed with error %d\n", startResult);
                    ma_device_uninit(&gDevice);
                    gDeviceInitialized = false;
                    return;
                }
            }
            gDeviceInitDeferred = false;
            gDeviceStartDeferred = false;
        }
    }

    // Ensure the device is started. Called on first audio operation on Windows.
    // On Windows, this runs device init on a background thread to avoid blocking the message pump.
    result miniaudio_ensure_thread_device_started()
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

        // Stop the device before uninitializing to ensure clean shutdown
        if (ma_device_get_state(&gDevice) == ma_device_state_started)
        {
            ma_device_stop(&gDevice);
        }

        // Lock the audio mutex to prevent race conditions during device change
        soloud->lockAudioMutex_internal();

        ma_device_uninit(&gDevice);
        gDeviceInitialized = false;

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.pDeviceID = (ma_device_id *)pPlaybackInfos_id;
        deviceConfig.periodSizeInFrames = soloud->mBufferSize;
        deviceConfig.playback.format    = ma_format_f32;
        deviceConfig.playback.channels  = soloud->mChannels;
        deviceConfig.sampleRate         = soloud->mSamplerate;
        deviceConfig.dataCallback       = soloud_miniaudio_audiomixer;
        deviceConfig.pUserData          = (void *)soloud;
        deviceConfig.notificationCallback = on_notification;

        // Preserve the performance profile chosen at init across device changes,
        // otherwise switching the output device would silently revert to the
        // default low-latency/MMAP path (see gMiniaudioLowLatency).
        deviceConfig.performanceProfile = gMiniaudioLowLatency
            ? ma_performance_profile_low_latency
            : ma_performance_profile_conservative;
#if defined(__ANDROID__)
        if (!gMiniaudioLowLatency)
        {
            deviceConfig.aaudio.usage                = ma_aaudio_usage_media;
            deviceConfig.aaudio.contentType          = ma_aaudio_content_type_music;
            deviceConfig.aaudio.allowedCapturePolicy = ma_aaudio_allow_capture_by_all;
        }
#endif

        ma_result result;
#if defined(MA_HAS_COREAUDIO) || defined(__ANDROID__)
        // Use the existing context on CoreAudio (macOS/iOS) and Android
        // to preserve session/category settings
        result = ma_device_init(&context, &deviceConfig, &gDevice);
#else
        // On other platforms, use NULL context (default behavior)
        result = ma_device_init(NULL, &deviceConfig, &gDevice);
#endif
        if (result != MA_SUCCESS)
        {
            gDeviceInitialized = false;
            soloud->unlockAudioMutex_internal();
            return UNKNOWN_ERROR;
        }

        gDeviceInitialized = true;
        gDeviceStopped = false;  // Device is about to start
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_changeDevice_impl: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            gDeviceInitialized = false;
            soloud->unlockAudioMutex_internal();
            return UNKNOWN_ERROR;
        }

        soloud->unlockAudioMutex_internal();
        return 0;
    }
};
#endif

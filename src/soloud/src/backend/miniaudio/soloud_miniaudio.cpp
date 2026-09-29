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

#ifdef __EMSCRIPTEN__
#include <cstring>
#include <pthread.h>
#include "soloud_thread.h"
#endif

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
#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>
#include "soloud_common.h"
#include "../../../../analyzer.h"
#include "../../../../mixeroutput/mixer_output.h"
#include "../../../../device_lifecycle_test_hooks.h"
#if defined(_WIN32) || defined(_WIN64)
#  include <windows.h>
#else
#  include <pthread.h>
#  include <sys/resource.h>
#endif

namespace SoLoud
{
    ma_device gDevice;
    std::atomic<SoLoud::Soloud *> gSoloud{nullptr};
    ma_context context;
    std::atomic<bool> gDeviceStopped{true};

    // Every operation that can initialize, start, stop, uninitialize, or
    // replace gDevice passes through this mutex. It is recursive because some
    // miniaudio backends can deliver notifications inline from an operation.
    static std::recursive_mutex gDeviceOperationMutex;

    // Invocation gate for device notifications.
    //
    // Storing nullptr into gSoloud stops a notification that has not started
    // yet; it does nothing for one that already loaded the pointer and was
    // then preempted. That notification goes on to dereference the Soloud --
    // and, on the interruption branch, the raw Player* held as the callback
    // context -- after teardown has freed both. miniaudio's own uninit does not
    // close the window either, because the load can happen before it.
    //
    // So notifications are admitted, and teardown retires admission and then
    // waits for admitted ones to finish before anything may be destroyed.
    //
    // The same boundary is drawn around a *device* replacement, not only around
    // engine teardown: replacing gDevice retires admission, drains, swaps, and
    // republishes. That is what orders every notification belonging to the old
    // device strictly before the replacement becomes current -- otherwise an
    // interruption admitted against device A could resume after device B had
    // taken its place and stop B because of an event belonging to a device that
    // no longer exists.
    //
    // What this does NOT do is identify the *origin* of a backend event. The
    // generation is assigned when on_notification() runs, not when the backend
    // produced the event, so it cannot recognize an A-event that first enters
    // this function after B is published. Safety there rests on a miniaudio
    // property instead: ma_device_uninit() stops the device and joins the
    // threads its notifications are delivered from, so once the swap above has
    // uninitialized A, A can no longer deliver. Admission is closed for the
    // whole window between uninit(A) and publish(B), so anything arriving in
    // between is refused. If a backend is ever added that can deliver a
    // notification after uninit returns, that assumption breaks and the event
    // needs an identity established at its origin -- a per-device callback
    // context rather than a generation read at dispatch time.
    //
    // Deliberately NOT gDeviceOperationMutex: notifications can be delivered
    // inline from device operations, so blocking on that lock here would need
    // a per-backend deadlock proof. This gate is only ever held for a handful
    // of instructions and never across the dispatch itself.
    static std::mutex gNotificationGateMutex;
    static std::condition_variable gNotificationGateCv;
    static int gNotificationsInFlight = 0;
    // gSoloud is the retirement state: it is written only by
    // publishNotificationTarget() and retireNotificationsAndDrain(), both under
    // gNotificationGateMutex, so "published" and "admitting" are the same
    // condition and cannot drift apart.

    struct NotificationPass
    {
        SoLoud::Soloud *soloud = nullptr;
        bool admitted = false;
    };

    /// Admit a notification and pin the engine it belongs to. A false return
    /// means notifications are retired (teardown owns the engine now) and the
    /// caller must touch nothing.
    static bool admitNotification(NotificationPass *pass)
    {
        std::lock_guard<std::mutex> lock(gNotificationGateMutex);
        SoLoud::Soloud *currentSoloud = gSoloud.load(std::memory_order_acquire);
        if (currentSoloud == nullptr)
            return false;

        ++gNotificationsInFlight;
        pass->soloud = currentSoloud;
        pass->admitted = true;
        return true;
    }

    static void releaseNotification(NotificationPass *pass)
    {
        if (!pass->admitted)
            return;
        pass->admitted = false;
        {
            std::lock_guard<std::mutex> lock(gNotificationGateMutex);
            --gNotificationsInFlight;
        }
        gNotificationGateCv.notify_all();
    }

    /// RAII wrapper, so no dispatch path can return without releasing.
    struct ScopedNotificationPass
    {
        NotificationPass pass;
        ScopedNotificationPass() { admitNotification(&pass); }
        ~ScopedNotificationPass() { releaseNotification(&pass); }
        ScopedNotificationPass(const ScopedNotificationPass &) = delete;
        ScopedNotificationPass &operator=(const ScopedNotificationPass &) = delete;
        explicit operator bool() const { return pass.admitted; }
        SoLoud::Soloud *operator->() const { return pass.soloud; }
    };

    /// Publish [aSoloud] as the engine notifications belong to, and open
    /// admission. Called once the engine is ready to receive them.
    static void publishNotificationTarget(SoLoud::Soloud *aSoloud)
    {
        std::lock_guard<std::mutex> lock(gNotificationGateMutex);
        gSoloud.store(aSoloud, std::memory_order_release);
    }

    /// Publishes the notification target for the duration of an
    /// initialization and retires it again unless that initialization
    /// commits. miniaudio_init() has several failure returns after the
    /// engine is published but before mBackendCleanupFunc is installed;
    /// without this, those paths would leave admission open on an engine that
    /// has no teardown hook left to close it.
    struct NotificationPublishGuard
    {
        bool committed = false;
        explicit NotificationPublishGuard(SoLoud::Soloud *aSoloud)
        {
            publishNotificationTarget(aSoloud);
        }
        ~NotificationPublishGuard();
        void commit() { committed = true; }
        NotificationPublishGuard(const NotificationPublishGuard &) = delete;
        NotificationPublishGuard &operator=(const NotificationPublishGuard &) = delete;
    };

    /// Close admission and wait for admitted notifications to finish. After
    /// this returns, no notification holds a pointer into the engine, so it
    /// may be torn down and destroyed.
    static void retireNotificationsAndDrain()
    {
        std::unique_lock<std::mutex> lock(gNotificationGateMutex);
        gSoloud.store(nullptr, std::memory_order_release);
        gNotificationGateCv.wait(lock, []
                                 { return gNotificationsInFlight == 0; });
    }

    NotificationPublishGuard::~NotificationPublishGuard()
    {
        if (!committed)
            retireNotificationsAndDrain();
    }

    /// Draws a notification-session boundary around a device replacement.
    ///
    /// Constructing it retires admission and waits out every notification
    /// already inside the old device; destroying it republishes for the
    /// replacement. Nothing belonging to the old device can therefore still be
    /// running once the new one is current -- which is the ordering a bare
    /// generation comparison could never establish, because rejecting a stale
    /// notification after the fact still lets it run concurrently with the
    /// swap.
    ///
    /// Republishes on every exit path, including failed swaps: the engine
    /// object is still alive and must keep receiving notifications, and the
    /// eventual teardown retires it again.
    struct DeviceSessionBoundary
    {
        SoLoud::Soloud *soloud;
        explicit DeviceSessionBoundary(SoLoud::Soloud *aSoloud) : soloud(aSoloud)
        {
            retireNotificationsAndDrain();
        }
        ~DeviceSessionBoundary() { publishNotificationTarget(soloud); }
        DeviceSessionBoundary(const DeviceSessionBoundary &) = delete;
        DeviceSessionBoundary &operator=(const DeviceSessionBoundary &) = delete;
    };
    
    // Selects the miniaudio performance profile used when (re)initializing the
    // device. Low-latency (the historical default) maps to AAudio's
    // PERFORMANCE_MODE_LOW_LATENCY / MMAP path on Android; that path can't be
    // captured by system screen recorders and leaves little callback headroom
    // for CPU-heavy filters. When this is false, the conservative (legacy
    // mixer) profile is used instead. Defined outside the WITH_MINIAUDIO guard
    // so the setter symbol always exists for the C bindings to call.
    static std::atomic<bool> gMiniaudioLowLatency{true};

    // Android (AAudio) stream attributes applied when low-latency is disabled.
    // Default to media/music (sensible for a media app, and capturable). When
    // [aManaged] is false the app wants to own AudioAttributes externally (e.g.
    // via the audio_session plugin), so we leave usage/contentType unset
    // (`_default`) and let AAudio pick its defaults. Stored in globals so the
    // SAME choice is re-applied on device changes (see both call sites) rather
    // than reverting. Defined outside the WITH_MINIAUDIO guard so the setter
    // symbol always exists for the C bindings to call.
    static ma_aaudio_usage gMiniaudioAAudioUsage = ma_aaudio_usage_media;
    static ma_aaudio_content_type gMiniaudioAAudioContentType = ma_aaudio_content_type_music;

    // Forward declarations for functions used in on_notification
    result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud);
    result soloud_miniaudio_resume(SoLoud::Soloud *aSoloud);
    result miniaudio_ensure_thread_device_started();
    static std::atomic<bool> gDeviceStartDeferred{false};
    static std::atomic<bool> gDeviceInitDeferred{false};
    static std::atomic<bool> gDeviceInitialized{false};
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

    // Serialization against miniaudio's AAudio reroute jobs.
    //
    // miniaudio does not reroute an AAudio stream inline. When the stream
    // reports an error (a disconnect, a route change), its error callback posts
    // a job to the context's job thread, and that job later closes gDevice's
    // stream and opens a replacement -- on the job thread, while this file
    // starts, stops, uninitializes and reinitializes the same gDevice from its
    // own. The job's only lock is gDevice.aaudio.rerouteLock, and its only
    // guard against a device going away is gDevice.aaudio.isTearingDown. Both
    // live inside gDevice: ma_device_uninit() destroys the lock and zeroes the
    // flag, and ma_device_init() zeroes both again before recreating the lock.
    // So a reroute that runs across a device replacement is serialized against
    // nothing. It opens a stream on a device that is half torn down or half
    // built, and the stream it replaces, or the one it opens, is left running
    // with nothing tracking it. Two streams then deliver data callbacks into
    // the one ma_device, whose fixed-size callback buffer is not thread safe,
    // or one outlives the buffers it reads, and the process dies in memcpy on
    // an AudioTrack thread.
    //
    // The job thread runs jobs one at a time, in the order they were posted.
    // So a job of our own that blocks keeps every reroute off gDevice until it
    // returns, and each operation on gDevice below holds the job thread that
    // way for its whole duration: a reroute queued before it finishes first,
    // and one queued while it runs (by the stream of a device being
    // initialized, say) runs after it, against a whole device. Which reroutes
    // get queued at all is the other half of this; see gateAAudioStreamError().
    //
    // True while the context has an AAudio job thread to hold. The context
    // outlives ma_context_uninit() with its backend still set, so this is what
    // stops a hold from being posted to a thread that no longer exists. By the
    // time it goes false, no stream is left for the gate to let an error
    // through for (see closeOrphanedDeviceStreams()). Guarded by
    // gDeviceOperationMutex.
    static bool gAAudioJobThreadLive = false;

#if defined(MA_HAS_AAUDIO)
    struct RerouteHoldState
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool holding = false;
        bool released = false;
    };

    static ma_result holdRerouteJobs(ma_job *pJob)
    {
        // Shared with the ScopedRerouteHold that posted this, so whichever of
        // the two is last to touch it frees it.
        auto *posted = reinterpret_cast<std::shared_ptr<RerouteHoldState> *>(
            pJob->data.custom.data0);
        const std::shared_ptr<RerouteHoldState> state = std::move(*posted);
        delete posted;

        std::unique_lock<std::mutex> lock(state->mutex);
        state->holding = true;
        state->cv.notify_all();
        state->cv.wait(lock, [&state] { return state->released; });
        return MA_SUCCESS;
    }
#endif

    /// While alive, no AAudio reroute job runs. Construct it with
    /// gDeviceOperationMutex held, before touching gDevice. A no-op on every
    /// other backend.
    class ScopedRerouteHold
    {
    public:
        ScopedRerouteHold()
        {
#if defined(MA_HAS_AAUDIO)
            if (!gAAudioJobThreadLive)
                return;
            // Reroute jobs dispatch their notifications on the job thread.
            // Nothing reached from there may operate the device (see
            // Soloud::_stateChangedCallback). Should something do so anyway,
            // don't make it hold the thread from itself, which would wait
            // forever.
            if (pthread_equal(pthread_self(), context.aaudio.jobThread.thread))
                return;

            auto state = std::make_shared<RerouteHoldState>();
            ma_job job = ma_job_init(MA_JOB_TYPE_CUSTOM);
            job.data.custom.proc = holdRerouteJobs;
            auto *posted = new std::shared_ptr<RerouteHoldState>(state);
            job.data.custom.data0 = (ma_uintptr)posted;

            // Posting fails only while the queue is full, and the queue drains
            // as the job thread works through it: wait for room, as below for
            // the thread itself. Never go ahead without the hold. Operating on
            // gDevice while a reroute can run is what this exists to prevent,
            // and a queue that full is when one is likeliest to.
            for (int attempts = 1;
                 ma_device_job_thread_post(&context.aaudio.jobThread, &job) !=
                 MA_SUCCESS;
                 attempts++)
            {
                if (attempts == 1000)
                    soloud_platform_log("miniaudio: still waiting for room in "
                                        "the AAudio job queue to hold "
                                        "reroutes\n");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            std::unique_lock<std::mutex> lock(state->mutex);
            state->cv.wait(lock, [&state] { return state->holding; });
            mState = std::move(state);
#endif
        }

        ~ScopedRerouteHold()
        {
#if defined(MA_HAS_AAUDIO)
            if (!mState)
                return;
            // Notified under the lock: the job thread must not see `released`
            // and return, dropping its reference, while this is still using
            // the condition variable.
            std::lock_guard<std::mutex> lock(mState->mutex);
            mState->released = true;
            mState->cv.notify_all();
#endif
        }

        ScopedRerouteHold(const ScopedRerouteHold &) = delete;
        ScopedRerouteHold &operator=(const ScopedRerouteHold &) = delete;

    private:
#if defined(MA_HAS_AAUDIO)
        std::shared_ptr<RerouteHoldState> mState;
#endif
    };

    // The performance profile gDevice was last initialized with. Set before
    // each ma_device_init() of it; a reroute reopens its stream with it.
    static std::atomic<int> gDevicePerformanceProfile{
        ma_performance_profile_low_latency};

#if defined(MA_HAS_AAUDIO)
    // Admission for reroutes.
    //
    // Holding the job thread orders reroutes against our operations, but it
    // cannot stop one being queued for a device that is gone by the time it
    // runs. miniaudio's error callback checks gDevice.aaudio.isTearingDown and
    // then queues a reroute of gDevice, and nothing ties either step to the
    // stream that reported the error:
    //
    // - The check can pass just before an operation marks the device tearing
    //   down and holds the job thread, so the reroute lands behind the hold and
    //   runs after the device has been replaced or removed.
    // - AAudio can report an error after the stream is closed: an idle legacy
    //   stream reports a route change from a binder thread that closing the
    //   stream does not wait for. By then gDevice has either been zeroed,
    //   and miniaudio's callback dereferences its null pContext, or become
    //   the replacement, whose fresh isTearingDown lets the reroute through.
    //
    // Either way a reroute meant for a stream that no longer exists reopens
    // the replacement device, with miniaudio's defaults, or runs against the
    // zeroed one, whose reroute lock was destroyed with it. So these streams'
    // error callbacks go through gateAAudioStreamError(), which only lets an
    // error through for a device stream this context has opened and not
    // closed, and does so under the same lock that closes streams and marks
    // the device tearing down.
    //
    // The AAudio entry points involved are interposed in `context`'s function
    // table, which miniaudio calls through, so miniaudio itself is unchanged.
    static MA_PFN_AAudioStreamBuilder_openStream gOpenAAudioStream;
    static MA_PFN_AAudioStream_close gCloseAAudioStream;
    static MA_PFN_AAudioStreamBuilder_setErrorCallback gSetAAudioErrorCallback;

    static std::mutex gAAudioStreamGate;
    // Guarded by gAAudioStreamGate, as is setting gDevice's isTearingDown:
    // the builders miniaudio has given an error callback, which are the ones
    // building gDevice's streams (the bare streams it opens to probe the
    // default device get none), and the streams those have opened that are
    // not yet closed.
    static std::vector<ma_AAudioStreamBuilder *> gDeviceStreamBuilders;
    static std::vector<ma_AAudioStream *> gOpenDeviceStreams;
    static ma_AAudioStream_errorCallback gMiniaudioErrorCallback = nullptr;

    /// The error callback AAudio calls for this context's streams, in place of
    /// miniaudio's. Not static: the AAudio reroute race test calls it the way
    /// AAudio does.
    void gateAAudioStreamError(ma_AAudioStream *pStream, void *pUserData,
                               ma_aaudio_result_t error)
    {
        // Held across miniaudio's callback, which checks isTearingDown and
        // queues the reroute without blocking: either it has queued it by the
        // time an operation marks the device tearing down -- ahead of that
        // operation's hold, to run first and find the flag set -- or it finds
        // the flag set itself.
        std::lock_guard<std::mutex> lock(gAAudioStreamGate);
        if (std::find(gOpenDeviceStreams.begin(), gOpenDeviceStreams.end(),
                      pStream) == gOpenDeviceStreams.end())
        {
            soloud_platform_log("miniaudio: ignoring AAudio error %d from a "
                                "closed stream\n",
                                error);
            return;
        }
        gMiniaudioErrorCallback(pStream, pUserData, error);
    }

    static ma_aaudio_result_t openAAudioStream(ma_AAudioStreamBuilder *pBuilder,
                                               ma_AAudioStream **ppStream)
    {
        const ma_aaudio_result_t result = gOpenAAudioStream(pBuilder, ppStream);
        std::lock_guard<std::mutex> lock(gAAudioStreamGate);
        const auto builder = std::find(gDeviceStreamBuilders.begin(),
                                       gDeviceStreamBuilders.end(), pBuilder);
        if (builder == gDeviceStreamBuilders.end())
            return result; // A probe: it has no error callback to gate.
        gDeviceStreamBuilders.erase(builder);
        // An error the stream reports before it is registered here is
        // ignored. The stream is then disconnected from the start, and AAudio
        // refuses to start it, as it would one that disconnected before it
        // was opened.
        if (result == MA_AAUDIO_OK)
            gOpenDeviceStreams.push_back(*ppStream);
        return result;
    }

    static ma_aaudio_result_t closeAAudioStream(ma_AAudioStream *pStream)
    {
        {
            std::lock_guard<std::mutex> lock(gAAudioStreamGate);
            gOpenDeviceStreams.erase(std::remove(gOpenDeviceStreams.begin(),
                                                 gOpenDeviceStreams.end(),
                                                 pStream),
                                     gOpenDeviceStreams.end());
        }
        // Not under the lock: closing joins the stream's callback thread,
        // which may be waiting for it in gateAAudioStreamError().
        return gCloseAAudioStream(pStream);
    }

    static void setAAudioErrorCallback(ma_AAudioStreamBuilder *pBuilder,
                                       ma_AAudioStream_errorCallback callback,
                                       void *pUserData)
    {
        {
            std::lock_guard<std::mutex> lock(gAAudioStreamGate);
            gMiniaudioErrorCallback = callback;
            gDeviceStreamBuilders.push_back(pBuilder);
        }
        gSetAAudioErrorCallback(pBuilder, gateAAudioStreamError, pUserData);
    }

    /// Not static: the AAudio reroute race test records the modes that reach
    /// AAudio.
    MA_PFN_AAudioStreamBuilder_setPerformanceMode gSetAAudioPerformanceMode;

    /// miniaudio reopens a rerouted stream from a fresh ma_device_config,
    /// whose zeroed performanceProfile is ma_performance_profile_low_latency.
    /// So every reroute would ask AAudio for its low-latency (MMAP) path,
    /// whatever the device was opened with, and the first route change would
    /// quietly undo lowLatency: false. Reroutes are the only thing that builds
    /// streams on the job thread: ask for the profile gDevice was initialized
    /// with there, and leave every other request as miniaudio makes it -- the
    /// device's own, and the low-latency hint it gives the streams it opens to
    /// probe the default device.
    static void setAAudioPerformanceMode(ma_AAudioStreamBuilder *pBuilder,
                                         ma_aaudio_performance_mode_t mode)
    {
        if (pthread_equal(pthread_self(), context.aaudio.jobThread.thread) &&
            gDevicePerformanceProfile.load(std::memory_order_acquire) ==
                ma_performance_profile_conservative)
            mode = MA_AAUDIO_PERFORMANCE_MODE_NONE; // What miniaudio maps it to.
        gSetAAudioPerformanceMode(pBuilder, mode);
    }

    /// Route a freshly initialized AAudio context's streams through the
    /// functions above, before it opens any.
    static void interposeAAudioStreams()
    {
        gOpenAAudioStream = (MA_PFN_AAudioStreamBuilder_openStream)
                                context.aaudio.AAudioStreamBuilder_openStream;
        gCloseAAudioStream =
            (MA_PFN_AAudioStream_close)context.aaudio.AAudioStream_close;
        gSetAAudioErrorCallback = (MA_PFN_AAudioStreamBuilder_setErrorCallback)
                                      context.aaudio.AAudioStreamBuilder_setErrorCallback;
        gSetAAudioPerformanceMode =
            (MA_PFN_AAudioStreamBuilder_setPerformanceMode)
                context.aaudio.AAudioStreamBuilder_setPerformanceMode;
        context.aaudio.AAudioStreamBuilder_openStream = (ma_proc)openAAudioStream;
        context.aaudio.AAudioStream_close = (ma_proc)closeAAudioStream;
        context.aaudio.AAudioStreamBuilder_setErrorCallback =
            (ma_proc)setAAudioErrorCallback;
        context.aaudio.AAudioStreamBuilder_setPerformanceMode =
            (ma_proc)setAAudioPerformanceMode;

        std::lock_guard<std::mutex> lock(gAAudioStreamGate);
        gDeviceStreamBuilders.clear();
        gOpenDeviceStreams.clear();
    }
#endif

    /// Tell reroute jobs to leave gDevice alone: its error callback stops
    /// posting them, and one already queued does nothing when it runs. For
    /// operations that are about to replace or remove the device, where a
    /// reroute is pointless; a queued one would otherwise run against the
    /// replacement, and reopen its stream with miniaudio's defaults rather
    /// than our configuration. Call before taking the ScopedRerouteHold, so
    /// that a reroute already on its way into the queue gets in ahead of the
    /// hold, and runs before the device goes.
    static void markDeviceTearingDown()
    {
#if defined(MA_HAS_AAUDIO)
        // gDevice.aaudio shares a union with the other backends.
        if (!gAAudioJobThreadLive)
            return;
        std::lock_guard<std::mutex> lock(gAAudioStreamGate);
        ma_atomic_bool32_set(&gDevice.aaudio.isTearingDown, MA_TRUE);
#endif
    }

    /// Close whatever device streams are still open once gDevice owns none:
    /// after it fails to initialize, and before the context goes away.
    ///
    /// ma_device_init() leaves the stream the backend opened open if a later
    /// step fails -- ma_device_post_init(), say, or allocating its buffers.
    /// The device is still "uninitialized" at that point, so the
    /// ma_device_uninit() it cleans up with returns without closing it, as
    /// would any later one. Nothing else would ever close that stream, and
    /// its errors would keep getting through the gate to miniaudio, after the
    /// context itself is gone.
    static void closeOrphanedDeviceStreams()
    {
#if defined(MA_HAS_AAUDIO)
        std::vector<ma_AAudioStream *> orphans;
        {
            std::lock_guard<std::mutex> lock(gAAudioStreamGate);
            orphans.swap(gOpenDeviceStreams);
        }
        for (ma_AAudioStream *pStream : orphans)
        {
            soloud_platform_log("miniaudio: closing an AAudio stream a failed "
                                "device initialization left open\n");
            gCloseAAudioStream(pStream);
        }
#endif
    }

    /// ma_device_uninit(), leaving reroutes marked off the zeroed device.
    /// ma_device_uninit() ends by zeroing gDevice, isTearingDown included, so
    /// a reroute queued while it ran -- one the stream reported before
    /// ma_device_uninit() set isTearingDown itself -- would otherwise open a
    /// stream on an empty device, one nothing would ever close. When that
    /// reroute runs it takes the device's reroute lock, zeroed along with it,
    /// which bionic defines as a statically initialized mutex, before it sees
    /// isTearingDown and does nothing. Call with a ScopedRerouteHold.
    static void uninitDevice()
    {
        ma_device_uninit(&gDevice);
        gDeviceInitialized.store(false, std::memory_order_release);
        markDeviceTearingDown();
    }

    // Added by Marco Bavagnoli
    void on_notification(const ma_device_notification* pNotification)
    {
        MA_ASSERT(pNotification != NULL);

        // Device-state notifications remain authoritative during teardown,
        // after the callback target has deliberately been cleared.
        if (pNotification->type == ma_device_notification_type_started)
            gDeviceStopped.store(false, std::memory_order_release);
        else if (pNotification->type == ma_device_notification_type_stopped)
            gDeviceStopped.store(true, std::memory_order_release);

        // Admit and pin the engine for the whole dispatch. A bare load of
        // gSoloud only rules out notifications that have not started; one that
        // already read the pointer would go on to dereference a destroyed
        // Soloud -- or, below, call through a freed Player* -- because teardown
        // has no way to know it is there.
        const ScopedNotificationPass pass;
        if (!pass)
            return;

        SOLOUD_TEST_BARRIER(deviceNotificationAdmitted);

        SoLoud::Soloud *currentSoloud = pass.pass.soloud;

        switch (pNotification->type)
        {
            case ma_device_notification_type_started:
            {
                currentSoloud->notifyStateChanged(0);
            } break;

            case ma_device_notification_type_stopped:
            {
                currentSoloud->notifyStateChanged(1);
            } break;

            case ma_device_notification_type_rerouted:
            {
                currentSoloud->notifyStateChanged(2);
            } break;

            case ma_device_notification_type_interruption_began:
            {
                auto interruptionCallback =
                    currentSoloud->_audioInterruptionCallback.load(
                        std::memory_order_acquire);
                void *interruptionContext =
                    currentSoloud->_audioInterruptionContext.load(
                        std::memory_order_acquire);
                if (interruptionCallback != nullptr &&
                    interruptionContext != nullptr)
                    interruptionCallback(interruptionContext, true);
                currentSoloud->notifyStateChanged(3);
            } break;

            case ma_device_notification_type_interruption_ended:
            {
                auto interruptionCallback =
                    currentSoloud->_audioInterruptionCallback.load(
                        std::memory_order_acquire);
                void *interruptionContext =
                    currentSoloud->_audioInterruptionContext.load(
                        std::memory_order_acquire);
                if (interruptionCallback != nullptr &&
                    interruptionContext != nullptr)
                    interruptionCallback(interruptionContext, false);
                currentSoloud->notifyStateChanged(4);
            } break;

            case ma_device_notification_type_unlocked:
            {
                currentSoloud->notifyStateChanged(5);
            } break;

            default: break;
        }
    }

    void miniaudio_debugTriggerAudioInterruption(bool aBegan)
    {
        if (!gDeviceInitialized.load(std::memory_order_acquire) ||
            gSoloud.load(std::memory_order_acquire) == nullptr)
            return;

        ma_device_notification notification = {};
        notification.pDevice = &gDevice;
        notification.type = aBegan
            ? ma_device_notification_type_interruption_began
            : ma_device_notification_type_interruption_ended;
        on_notification(&notification);
    }

    void miniaudio_setLowLatency(bool aLowLatency)
    {
        std::lock_guard<std::recursive_mutex> lock(gDeviceOperationMutex);
        gMiniaudioLowLatency.store(aLowLatency, std::memory_order_release);
    }

    void miniaudio_setAndroidAAudioAttributes(bool aManaged)
    {
        std::lock_guard<std::recursive_mutex> lock(gDeviceOperationMutex);
        gMiniaudioAAudioUsage =
            aManaged ? ma_aaudio_usage_media : ma_aaudio_usage_default;
        gMiniaudioAAudioContentType =
            aManaged ? ma_aaudio_content_type_music : ma_aaudio_content_type_default;
    }

#if (defined(__linux__) || defined(__LINUX__)) && !defined(__ANDROID__)
    static std::atomic<int> gLinuxAudioBackend{0};
#endif

    void miniaudio_setLinuxAudioBackend(int aBackend)
    {
#if (defined(__linux__) || defined(__LINUX__)) && !defined(__ANDROID__)
        std::lock_guard<std::recursive_mutex> lock(gDeviceOperationMutex);
        gLinuxAudioBackend.store(aBackend, std::memory_order_release);
        const char *backendName = "Auto (ALSA -> PulseAudio -> JACK)";
        if (aBackend == 1) backendName = "ALSA";
        else if (aBackend == 2) backendName = "PulseAudio";
        else if (aBackend == 3) backendName = "JACK";
        soloud_platform_log("miniaudio: Linux audio backend set to %s (%d)\n", backendName, aBackend);
#else
        soloud_platform_log("miniaudio: setLinuxAudioBackend ignored (not Linux)\n");
#endif
    }

    int miniaudio_getLinuxAudioBackend()
    {
#if (defined(__linux__) || defined(__LINUX__)) && !defined(__ANDROID__)
        return gLinuxAudioBackend.load(std::memory_order_acquire);
#else
        return 0;
#endif
    }

    // The single place that decides which backends, and which context
    // settings, a context gets on this platform. Both the engine's `context`
    // and the temporary one device enumeration falls back to are created here,
    // so they cannot drift apart: device IDs belong to the backend that
    // enumerated them, so a list built on another backend hands out IDs the
    // engine's context cannot open.
    static ma_result init_platform_context(ma_context *aContext)
    {
        ma_context_config contextConfig = ma_context_config_init();
#if defined(MA_HAS_COREAUDIO)
        // Leave the AVAudioSession to the app: a default context would set its
        // category and activate it, and then deactivate it on uninit.
        contextConfig.coreaudio.sessionCategory = ma_ios_session_category_none;
        contextConfig.coreaudio.noAudioSessionActivate = true;
        contextConfig.coreaudio.noAudioSessionDeactivate = true;
        return ma_context_init(NULL, 0, &contextConfig, aContext);
#elif defined(__ANDROID__)
        // OpenSL only on Android <= 10, where uninitializing an AAudio device
        // can crash (see the MA_NO_AAUDIO note at the top of this file).
        // miniaudio's default order would pick AAudio from API 27.
        ma_backend backends[] = { ma_backend_aaudio, ma_backend_opensl };
        ma_uint32 backendCount = 2;
        if (android_get_device_api_level() <= 29) {
            backends[0] = ma_backend_opensl;
            backendCount = 1;
        }
        return ma_context_init(backends, backendCount, &contextConfig, aContext);
#elif defined(__linux__) || defined(__LINUX__)
        ma_backend backends[3];
        ma_uint32 backendCount = 0;
        const int chosenBackend = gLinuxAudioBackend.load(std::memory_order_acquire);
        if (chosenBackend == 1) { // ALSA
            backends[0] = ma_backend_alsa;
            backendCount = 1;
        } else if (chosenBackend == 2) { // PulseAudio
            backends[0] = ma_backend_pulseaudio;
            backendCount = 1;
        } else if (chosenBackend == 3) { // JACK
            backends[0] = ma_backend_jack;
            backendCount = 1;
        } else { // Auto: ALSA first, then PulseAudio, then JACK
            backends[0] = ma_backend_alsa;
            backends[1] = ma_backend_pulseaudio;
            backends[2] = ma_backend_jack;
            backendCount = 3;
        }
        return ma_context_init(backends, backendCount, &contextConfig, aContext);
#else
        // Other platforms open the device without a context of their own, which
        // makes miniaudio create one with this same default configuration.
        return ma_context_init(NULL, 0, &contextConfig, aContext);
#endif
    }

    // Whether `context` is currently initialized. Only the platforms handled
    // by init_platform_context() above ever open it. Guarded by
    // gDeviceOperationMutex.
    static bool gEngineContextInitialized = false;

    // Unused where the device is opened without a context (Windows, web).
    [[maybe_unused]] static ma_result open_engine_context()
    {
        const ma_result result = init_platform_context(&context);
        gEngineContextInitialized = result == MA_SUCCESS;
        gAAudioJobThreadLive =
            gEngineContextInitialized && context.backend == ma_backend_aaudio;
#if defined(MA_HAS_AAUDIO)
        if (gAAudioJobThreadLive)
            interposeAAudioStreams();
#endif
        return result;
    }

    static void close_engine_context()
    {
        if (!gEngineContextInitialized)
            return;
        closeOrphanedDeviceStreams();
        gAAudioJobThreadLive = false;
        ma_context_uninit(&context);
        gEngineContextInitialized = false;
    }

    // The playback devices of `aContext`, copied out: the array miniaudio
    // returns belongs to the context and is overwritten by the next
    // enumeration on it.
    static ma_result copy_playback_devices(ma_context *aContext,
                                           std::vector<ma_device_info> &aDevices)
    {
        ma_device_info *pPlaybackInfos;
        ma_uint32 playbackCount;
        ma_device_info *pCaptureInfos;
        ma_uint32 captureCount;
        const ma_result result = ma_context_get_devices(
            aContext, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount);
        if (result != MA_SUCCESS)
            return result;
        aDevices.assign(pPlaybackInfos, pPlaybackInfos + playbackCount);
        return MA_SUCCESS;
    }

    ma_result miniaudio_listPlaybackDevices(std::vector<ma_device_info> &aDevices)
    {
        aDevices.clear();
        // Held throughout, including around the temporary context below.
        // Enumeration runs on the UI isolate while initEngine() runs on a
        // worker, and the engine context is only ever opened under this
        // mutex; so the two contexts never exist at once (miniaudio allows a
        // single OpenSL|ES context), and an enumeration that arrives during
        // init waits for it and then uses the engine's context.
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);

        // With the engine running, enumerate on its own context: that is the
        // backend the returned IDs will be opened on.
        if (gEngineContextInitialized)
            return copy_playback_devices(&context, aDevices);

        // No engine context yet, so build a temporary one the way the engine
        // will build its own.
        ma_context enumerationContext;
        ma_result result = init_platform_context(&enumerationContext);
        if (result != MA_SUCCESS)
            return result;
        result = copy_playback_devices(&enumerationContext, aDevices);
        ma_context_uninit(&enumerationContext);
        return result;
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
        const unsigned int outChannels = pDevice->playback.channels;
#ifdef __EMSCRIPTEN__
        // Stale-callback guard: on the web the miniaudio device is a global,
        // re-used across engine sessions, and a stale AudioWorklet (or a
        // ScriptProcessorNode callback) from a previous session can still
        // fire while the engine is being torn down or re-initialized.
        // pUserData then points at the new, not yet fully initialized engine
        // (or, while the backend global gSoloud is cleared, at nothing
        // valid), and mixing would touch half-initialized or freed engine
        // state. Emit silence instead.
        if (soloud == nullptr ||
            soloud != gSoloud.load(std::memory_order_acquire) ||
            soloud->mEngineReady == 0)
        {
            std::memset(pOutput, 0,
                        frameCount * (outChannels != 0 ? outChannels : 1) *
                            sizeof(float));
            return;
        }
        // On the multi-threaded (AudioWorklet) build this callback runs on
        // the AudioWorklet rendering thread, where blocking is forbidden:
        // a contended lock that lowers to a futex wait aborts the whole
        // module (futex waits are illegal on AudioWorklet threads). The main
        // browser thread takes the audio mutex for every engine API call
        // (play, pause, getters...), so contention is routine. Try to take
        // the mutex instead; on contention emit silence for this block and
        // retry on the next one. On the MT build Thread::createMutex returns
        // a custom recursive spin mutex (see soloud_thread.cpp): musl's
        // pthread mutexes are unusable on the AudioWorklet thread because
        // __pthread_self() is null there, so their owner bookkeeping both
        // fails to exclude other threads and misdetects recursive re-entry.
        // The custom mutex is recursive, so when the try-lock succeeds the
        // lock/unlock inside mix() simply re-enters it. On the main thread
        // (single-threaded build) the try-lock always succeeds because the
        // mutex can only be held by this same thread, so behavior there is
        // unchanged.
        if (SoLoud::Thread::tryLockMutex(soloud->mAudioThreadMutex) != 0)
        {
            std::memset(pOutput, 0,
                        frameCount * (outChannels != 0 ? outChannels : 1) *
                            sizeof(float));
            return;
        }
        soloud->mix((float *)pOutput, frameCount);
        MixerOutput::instance().onAudioData((float *)pOutput, frameCount);
        Analyzer::instance().onAudioData((float *)pOutput, frameCount, outChannels);
        // Use the SoLoud unlock (not raw pthread_mutex_unlock) so the
        // ended-voice queue is drained with correct bookkeeping.
        soloud->unlockAudioMutex_internal();
#else
        if (soloud->isRenderAheadEnabled())
        {
            // Render-ahead ring path: the engine has already mixed
            // renderAheadFrames into its own ring; this callback only copies
            // out and tops the ring back up. The ring read is lock-free SPSC,
            // so the device never blocks behind the audio mutex.
            soloud->renderRingTopUp_internal();
            unsigned int got =
                soloud->mRenderRing.read((float *)pOutput, frameCount);
            if (got < frameCount)
            {
                // Underrun: emit silence for the gap. Recoverable on the next
                // callback -- the top-up above keeps producing.
                static bool underrunLogged = false;
                if (!underrunLogged)
                {
                    underrunLogged = true;
                    soloud_platform_log("soloud_miniaudio_audiomixer: render ring underrun (%u/%u frames)\n", got, frameCount);
                }
                unsigned int outCh = pDevice->playback.channels;
                std::memset((float *)pOutput + (size_t)got * outCh, 0,
                            (size_t)(frameCount - got) * outCh * sizeof(float));
            }
            // The capture tap sits after the ring read, so it always reflects
            // what was actually played, including any retroactive rewrite.
            MixerOutput::instance().onAudioData((float *)pOutput, frameCount);
            Analyzer::instance().onAudioData((float *)pOutput, frameCount, outChannels);
        }
        else
        {
            soloud->mix((float *)pOutput, frameCount);
            MixerOutput::instance().onAudioData((float *)pOutput, frameCount);
            Analyzer::instance().onAudioData((float *)pOutput, frameCount, outChannels);
        }
#endif
    }

    static void soloud_miniaudio_deinit(SoLoud::Soloud *aSoloud)
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);

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

        // Close notification admission and wait for any notification already
        // inside the engine to finish, BEFORE uninitializing the device.
        // Clearing the pointer alone only stops notifications that have not
        // started yet; this also waits out the ones that already hold it, which
        // is what lets the caller destroy the Soloud and its owning Player
        // afterwards. Released before ma_device_uninit() below, so a "stopped"
        // notification delivered inline from it finds admission closed and
        // returns rather than deadlocking on the gate.
        retireNotificationsAndDrain();

        if (gDeviceInitialized.load(std::memory_order_acquire))
        {
            // Nothing will be left to reroute, and the job thread goes away
            // with the context below.
            markDeviceTearingDown();
            const ScopedRerouteHold rerouteHold;

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
                while (!gDeviceStopped.load(std::memory_order_acquire) &&
                       timeoutMs < maxTimeoutMs)
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
            gDeviceStopped.store(true, std::memory_order_release);
            
            // From miniaudio.h doc:
            // "This will explicitly stop the device. You do not need to call `ma_device_stop()` beforehand, but it's harmless if you do."
            uninitDevice();
        }
        // A no-op if it is already closed, e.g. after a failed Linux backend
        // switch, or on platforms that never open it.
        close_engine_context();
    }

    // Pause the audio device: stops the CoreAudio AudioUnit (or platform equivalent)
    // without uninitialising it. This is the correct way to "pause" on iOS/macOS —
    // it tells the OS the app has nothing to render, which preserves AVAudioSession
    // state and keeps MPRemoteCommandCenter routing intact.
    result soloud_miniaudio_pause(SoLoud::Soloud *aSoloud)
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        if (!gDeviceInitialized.load(std::memory_order_acquire))
            return 0; // No device to pause.
        const ScopedRerouteHold rerouteHold;

        if (ma_device_get_state(&gDevice) == ma_device_state_started)
        {
#if defined(__EMSCRIPTEN__)
            /* On Web, don't suspend the audio device to avoid a bug where
               stale buffered audio data can fire after the device is stopped but before
               it takes effect. When stop() and play() are called in quick succession,
               those stale buffers get queued and play after resume(), causing audio
               glitches and lag. Keeping the device running is safe: soloud->mix()
               produces silence when no voices are active, which has negligible overhead.
               This solves #446 on Web. */
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
        // Serialize against device teardown in soloud_miniaudio_deinit(), the
        // swap in miniaudio_changeDevice_impl(), and the explicit start/stop
        // entry points.
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);

        if (aSoloud == nullptr)
            return UNKNOWN_ERROR;

        if (!gDeviceInitialized.load(std::memory_order_acquire))
            return UNKNOWN_ERROR;

        // On Android, starting the device while an AAudio reroute is closing
        // the old stream crashes with SIGABRT (CFI) inside
        // AAudioStream_waitForStateChange.
        const ScopedRerouteHold rerouteHold;

        // Check if device is stopped and start it if needed
        ma_result result = MA_SUCCESS;
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
        // Lets a test drive the rebuild/retry path and the failure reporting
        // behind it without needing hardware that can actually fail.
        if (soloud_test::consumeForcedDeviceStartFailure())
            return UNKNOWN_ERROR;
#endif

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
            //
            // If the session cannot be activated (another app is holding an
            // exclusive session, the interruption is still ongoing, ...) then
            // starting the Audio Unit would leave it running against an
            // inactive session and produce no audio. Report the failure to the
            // caller instead of pretending the device was resumed.
            bool sessionActivated = true;
            @autoreleasepool
            {
                NSError *sessionError = nil;
                sessionActivated = [[AVAudioSession sharedInstance] setActive:YES
                                                                       error:&sessionError] == YES;
            }
            if (!sessionActivated)
            {
                result = MA_ERROR;
            }
            else
#endif
            {
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
                soloud_test::recordBackendDeviceStart();
#endif
                result = ma_device_start(&gDevice);
            }
        }
        return result == MA_SUCCESS ? 0 : UNKNOWN_ERROR;
    }

    // Unconditionally stop the miniaudio output device, regardless of platform
    // idle-pause policy or whether voices are still active. Only the device is
    // touched: SoLoud is not deinitialised and its voices/sources are left
    // untouched, so miniaudio_startAudioDevice() can resume rendering exactly
    // where it left off. Idempotent: a no-op if the device is already stopped.
    result miniaudio_stopAudioDevice()
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        const ScopedRerouteHold rerouteHold;

        if (ma_device_get_state(&gDevice) == ma_device_state_started)
        {
            ma_result res = ma_device_stop(&gDevice);
            if (res != MA_SUCCESS)
                return UNKNOWN_ERROR;
        }
        return 0;
    }

    // Restart the miniaudio output device previously stopped by
    // miniaudio_stopAudioDevice(). Idempotent: a no-op if the device is already
    // started.
    result miniaudio_startAudioDevice()
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        const ScopedRerouteHold rerouteHold;

        if (ma_device_get_state(&gDevice) == ma_device_state_stopped)
        {
            ma_result res = ma_device_start(&gDevice);
            if (res != MA_SUCCESS)
                return UNKNOWN_ERROR;
        }
        return 0;
    }

    // Return the current state of the miniaudio output device as the raw
    // ma_device_state value. When the device has not been initialized there is
    // no valid device to query, so report ma_device_state_uninitialized.
    unsigned int miniaudio_getAudioDeviceState()
    {
        if (!gDeviceInitialized.load(std::memory_order_acquire))
            return ma_device_state_uninitialized;
        return (unsigned int)ma_device_get_state(&gDevice);
    }

    // Engine mix quantum to hand to postinit_internal. With the render-ahead
    // ring the device period is decoupled from the engine quantum, so the
    // engine must get the *configured* quantum (aBuffer), not the device
    // period miniaudio actually negotiated. Without the ring the historical
    // behavior (actual device period) is kept.
    static unsigned int postinit_buffer_size(SoLoud::Soloud *aSoloud,
                                             unsigned int aConfiguredBuffer,
                                             unsigned int aDeviceInternalPeriod)
    {
        return aSoloud->isRenderAheadEnabled() ? aConfiguredBuffer
                                               : aDeviceInternalPeriod;
    }

    result miniaudio_init(SoLoud::Soloud *aSoloud, unsigned int aFlags, unsigned int aSamplerate, unsigned int aBuffer, unsigned int aChannels, void *pPlaybackInfos_id)
    {
        std::unique_lock<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        // Opens notification admission as well as publishing the pointer.
        // Retired again by the guard on any failure return below.
        NotificationPublishGuard notificationGuard(aSoloud);
        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        if (pPlaybackInfos_id != NULL)
        {
            deviceConfig.playback.pDeviceID = (ma_device_id*)pPlaybackInfos_id;
        }
        deviceConfig.periodSizeInFrames =
            aSoloud->isRenderAheadEnabled() ? aSoloud->mDevicePeriodFrames : aBuffer;
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
        deviceConfig.performanceProfile = gMiniaudioLowLatency.load(std::memory_order_acquire)
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

        // The initialization thread acquires the operation mutex itself. Drop
        // this thread's ownership while waiting for it so the actual device
        // initialization and start still pass through the serialization point.
        operationLock.unlock();
        miniaudio_ensure_thread_device_started();
        operationLock.lock();

#elif defined(MA_HAS_COREAUDIO)
        if (open_engine_context() != MA_SUCCESS) {
            return UNKNOWN_ERROR;
        }
        if (ma_device_init(&context, &deviceConfig, &gDevice) != MA_SUCCESS)
        {
            close_engine_context();
            return UNKNOWN_ERROR;
        }
        gDeviceInitialized = true;
        aSoloud->postinit_internal(gDevice.sampleRate, postinit_buffer_size(aSoloud, aBuffer, gDevice.playback.internalPeriodSizeInFrames), aFlags, gDevice.playback.channels);
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            close_engine_context();
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
        
#elif defined(__ANDROID__)
        // When low-latency is disabled the device runs on the legacy mixer path
        // (set above). Tag the AAudio stream with the configured usage/contentType
        // (media/music by default; left unset when the app opts to manage
        // AudioAttributes externally via audio_session — see
        // miniaudio_setAndroidAAudioAttributes) and explicitly allow capture so
        // system screen recorders pick up the audio. miniaudio skips the setter
        // for `_default`, so opting out truly leaves the attributes untouched.
        // The same globals are re-applied on device changes (changeDevice_impl).
        if (!gMiniaudioLowLatency.load(std::memory_order_acquire))
        {
            deviceConfig.aaudio.usage                = gMiniaudioAAudioUsage;
            deviceConfig.aaudio.contentType          = gMiniaudioAAudioContentType;
            deviceConfig.aaudio.allowedCapturePolicy = ma_aaudio_allow_capture_by_all;
        }

        if (open_engine_context() != MA_SUCCESS) {
            return UNKNOWN_ERROR;
        }
        ma_result startResult = MA_ERROR;
        {
            // The new stream can report an error, and so queue a reroute,
            // before ma_device_init() has finished building the device.
            const ScopedRerouteHold rerouteHold;
            gDevicePerformanceProfile.store(deviceConfig.performanceProfile,
                                            std::memory_order_release);
            if (ma_device_init(&context, &deviceConfig, &gDevice) != MA_SUCCESS) {
                markDeviceTearingDown();
                closeOrphanedDeviceStreams();
            } else {
                gDeviceInitialized = true;
                aSoloud->postinit_internal(gDevice.sampleRate, postinit_buffer_size(aSoloud, aBuffer, gDevice.playback.internalPeriodSizeInFrames), aFlags, gDevice.playback.channels);
                startResult = ma_device_start(&gDevice);
                if (startResult != MA_SUCCESS) {
                    soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
                    uninitDevice();
                }
            }
        }
        if (startResult != MA_SUCCESS) {
            close_engine_context();
            return UNKNOWN_ERROR;
        }
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;

#elif defined(__linux__) || defined(__LINUX__)
        ma_result result = open_engine_context();
        if (result != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_context_init failed with error %d\n", result);
            return UNKNOWN_ERROR;
        }
        if (ma_device_init(&context, &deviceConfig, &gDevice) != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_init failed\n");
            close_engine_context();
            return UNKNOWN_ERROR;
        }
        gDeviceInitialized = true;
        aSoloud->postinit_internal(gDevice.sampleRate, postinit_buffer_size(aSoloud, aBuffer, gDevice.playback.internalPeriodSizeInFrames), aFlags, gDevice.playback.channels);
        ma_result startResult = ma_device_start(&gDevice);
        if (startResult != MA_SUCCESS) {
            soloud_platform_log("miniaudio_init: ma_device_start failed with error %d\n", startResult);
            ma_device_uninit(&gDevice);
            close_engine_context();
            gDeviceInitialized = false;
            return UNKNOWN_ERROR;
        }
        gDeviceInitDeferred = false;
        gDeviceStartDeferred = false;
        const char *activeBackendName = ma_get_backend_name(context.backend);
        soloud_platform_log("miniaudio_init: audio device initialized using Linux backend: %s\n", activeBackendName ? activeBackendName : "unknown");

#else
        // Other platforms
        ma_result deviceInitResult = ma_device_init(NULL, &deviceConfig, &gDevice);
        if (deviceInitResult != MA_SUCCESS)
        {
            soloud_platform_log("miniaudio_init: ma_device_init failed with error %d\n", deviceInitResult);
            return UNKNOWN_ERROR;
        }
        gDeviceInitialized = true;
#ifdef __EMSCRIPTEN__
        // On the web the engine buffer size must match what miniaudio's
        // fixed-size callback aggregation actually delivers to the data
        // callback, which is `periodSizeInFrames` from the device config
        // (aBuffer). With the AudioWorklet backend the async worklet startup
        // rewrites the descriptor and shrinks `internalPeriodSizeInFrames` to
        // the 128-frame render quantum; using that value would misreport the
        // engine buffer size (it drives scheduling, timing and buffer sizing
        // throughout the engine) while the data callback is invoked with
        // aBuffer frames. With the ScriptProcessorNode backend
        // aBuffer == internalPeriodSizeInFrames, so this is a no-op there.
        // When aBuffer is 0 (AUTO) both the aggregation and SoLoud fall back
        // to the internal period, so use it.
        const unsigned int postinitBufferSize =
            aBuffer != 0 ? aBuffer : gDevice.playback.internalPeriodSizeInFrames;
        aSoloud->postinit_internal(gDevice.sampleRate, postinitBufferSize, aFlags, gDevice.playback.channels);
#else
        aSoloud->postinit_internal(gDevice.sampleRate, postinit_buffer_size(aSoloud, aBuffer, gDevice.playback.internalPeriodSizeInFrames), aFlags, gDevice.playback.channels);
#endif
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

        // The engine now owns a teardown hook that will retire notifications,
        // so the guard must not do it on the way out.
        notificationGuard.commit();
        aSoloud->mBackendCleanupFunc = soloud_miniaudio_deinit;
        aSoloud->mBackendPauseFunc   = soloud_miniaudio_pause;
        aSoloud->mBackendResumeFunc  = soloud_miniaudio_resume;
        aSoloud->mBackendString = "MiniAudio";
        return 0;
    }

    // Background thread function to initialize the audio device
    static void miniaudio_init_thread_func()
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
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
        if (!gDeviceInitDeferred.load(std::memory_order_acquire))
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
        if (gDeviceInitDeferred.load(std::memory_order_acquire))
            return UNKNOWN_ERROR; // Init failed
            
        return 0;
    }

    result miniaudio_changeDevice_impl(void *pPlaybackInfos_id)
    {
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        SoLoud::Soloud *currentSoloud =
            gSoloud.load(std::memory_order_acquire);
        if (currentSoloud == nullptr)
            return UNKNOWN_ERROR;

        // The device is about to be replaced, so there is nothing for a
        // reroute to do; one queued now would only run against the
        // replacement. The hold outlives the session boundary below, so a
        // reroute the replacement's stream queues during its initialization
        // runs once notifications are open again, and its "rerouted" is not
        // dropped.
        markDeviceTearingDown();
        const ScopedRerouteHold rerouteHold;

        // Every caller that replaces gDevice comes through here -- the public
        // changeDevice() and the stale-device rebuild inside
        // performAudioDeviceStart() alike -- so this one boundary covers them
        // all.
        DeviceSessionBoundary sessionBoundary(currentSoloud);

        // Stop the device before uninitializing to ensure clean shutdown
        if (ma_device_get_state(&gDevice) != ma_device_state_stopped)
        {
            ma_device_stop(&gDevice);
        }

        // SoLoud's audio-thread mutex is deliberately NOT held across the swap
        // below, even though it guards the mixer, because it does not protect
        // `gDevice`: the data callback reaches the engine through
        // `pDevice->pUserData` and takes that mutex itself inside
        // `Soloud::mix()`, and `ma_device_uninit()` already guarantees the
        // callback has stopped before it returns. Holding it here only starves
        // the audio thread, and on Android that is fatal: on AAudio's legacy
        // (non-MMAP) path a stream reports STARTED only once its first data
        // callback has run, so a held mutex makes that callback block,
        // `ma_device_start()` time out after 5s, and the cleanup
        // `ma_device_uninit()` then wait forever on the very callback the
        // caller is blocking. That deadlock is the Android ANR.
        uninitDevice();
        // No device exists between the uninit and the init below, so don't
        // leave `deinit()` polling for a "stopped" notification that can no
        // longer arrive.
        gDeviceStopped.store(true, std::memory_order_release);

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.pDeviceID = (ma_device_id *)pPlaybackInfos_id;
        // With the render-ahead ring the device period is the small configured
        // one, decoupled from the engine mix quantum (mBufferSize).
        deviceConfig.periodSizeInFrames =
            currentSoloud->isRenderAheadEnabled()
                ? currentSoloud->mDevicePeriodFrames
                : currentSoloud->mBufferSize;
        deviceConfig.playback.format    = ma_format_f32;
        deviceConfig.playback.channels  = currentSoloud->mChannels;
        deviceConfig.sampleRate         = currentSoloud->mSamplerate;
        deviceConfig.dataCallback       = soloud_miniaudio_audiomixer;
        deviceConfig.pUserData          = (void *)currentSoloud;
        deviceConfig.notificationCallback = on_notification;

        // Preserve the performance profile chosen at init across device changes,
        // otherwise switching the output device would silently revert to the
        // default low-latency/MMAP path (see gMiniaudioLowLatency).
        deviceConfig.performanceProfile = gMiniaudioLowLatency.load(std::memory_order_acquire)
            ? ma_performance_profile_low_latency
            : ma_performance_profile_conservative;
#if defined(__ANDROID__)
        if (!gMiniaudioLowLatency.load(std::memory_order_acquire))
        {
            // Re-apply the SAME attributes chosen at init so a device change
            // doesn't silently revert them. If the app opted out
            // (miniaudio_setAndroidAAudioAttributes(false)), these are `_default`
            // and miniaudio leaves them unset — so an externally-managed
            // configuration (e.g. via audio_session) is preserved across device
            // changes rather than being forced back to media/music.
            deviceConfig.aaudio.usage                = gMiniaudioAAudioUsage;
            deviceConfig.aaudio.contentType          = gMiniaudioAAudioContentType;
            deviceConfig.aaudio.allowedCapturePolicy = ma_aaudio_allow_capture_by_all;
        }
#endif

        ma_result result;
        gDevicePerformanceProfile.store(deviceConfig.performanceProfile,
                                        std::memory_order_release);
#if defined(MA_HAS_COREAUDIO) || defined(__ANDROID__) || defined(__linux__) || defined(__LINUX__)
        // Use the existing context on CoreAudio (macOS/iOS), Android, and Linux
        // to preserve session/category/backend settings
        result = ma_device_init(&context, &deviceConfig, &gDevice);
#else
        // On other platforms, use NULL context (default behavior)
        result = ma_device_init(NULL, &deviceConfig, &gDevice);
#endif
        if (result != MA_SUCCESS)
        {
            soloud_platform_log(
                "miniaudio_changeDevice_impl: ma_device_init failed with error %d\n",
                result);
            gDeviceInitialized.store(false, std::memory_order_release);
            markDeviceTearingDown();
            closeOrphanedDeviceStreams();
            return UNKNOWN_ERROR;
        }

        gDeviceInitialized.store(true, std::memory_order_release);
        gDeviceStopped.store(true, std::memory_order_release);
        // Leave the replacement device stopped. Player's serialized lifecycle
        // coordinator decides whether active playback, an in-flight timeout,
        // or indefinite keep-alive policy requires it to be started.
        return 0;
    }

    result miniaudio_changeLinuxBackend_impl(int aBackend)
    {
#if (defined(__linux__) || defined(__LINUX__)) && !defined(__ANDROID__)
        std::lock_guard<std::recursive_mutex> operationLock(gDeviceOperationMutex);
        SoLoud::Soloud *currentSoloud =
            gSoloud.load(std::memory_order_acquire);
        if (currentSoloud == nullptr)
            return UNKNOWN_ERROR;

        gLinuxAudioBackend.store(aBackend, std::memory_order_release);

        DeviceSessionBoundary sessionBoundary(currentSoloud);

        if (ma_device_get_state(&gDevice) != ma_device_state_stopped)
        {
            ma_device_stop(&gDevice);
        }

        ma_device_uninit(&gDevice);
        gDeviceInitialized.store(false, std::memory_order_release);
        gDeviceStopped.store(true, std::memory_order_release);

        close_engine_context();

        // Picks up aBackend, stored in gLinuxAudioBackend above.
        ma_result ctxRes = open_engine_context();
        if (ctxRes != MA_SUCCESS) {
            soloud_platform_log("miniaudio_changeLinuxBackend_impl: ma_context_init failed with error %d\n", ctxRes);
            return UNKNOWN_ERROR;
        }

        ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
        deviceConfig.playback.pDeviceID = nullptr;
        deviceConfig.periodSizeInFrames =
            currentSoloud->isRenderAheadEnabled()
                ? currentSoloud->mDevicePeriodFrames
                : currentSoloud->mBufferSize;
        deviceConfig.playback.format    = ma_format_f32;
        deviceConfig.playback.channels  = currentSoloud->mChannels;
        deviceConfig.sampleRate         = currentSoloud->mSamplerate;
        deviceConfig.dataCallback       = soloud_miniaudio_audiomixer;
        deviceConfig.pUserData          = (void *)currentSoloud;
        deviceConfig.notificationCallback = on_notification;

        deviceConfig.performanceProfile = gMiniaudioLowLatency.load(std::memory_order_acquire)
            ? ma_performance_profile_low_latency
            : ma_performance_profile_conservative;

        ma_result devRes = ma_device_init(&context, &deviceConfig, &gDevice);
        if (devRes != MA_SUCCESS)
        {
            soloud_platform_log(
                "miniaudio_changeLinuxBackend_impl: ma_device_init failed with error %d\n",
                devRes);
            close_engine_context();
            gDeviceInitialized.store(false, std::memory_order_release);
            return UNKNOWN_ERROR;
        }

        gDeviceInitialized.store(true, std::memory_order_release);
        gDeviceStopped.store(true, std::memory_order_release);
        const char *activeBackendName = ma_get_backend_name(context.backend);
        soloud_platform_log("miniaudio_changeLinuxBackend_impl: switched to Linux backend: %s\n", activeBackendName ? activeBackendName : "unknown");
        return 0;
#else
        (void)aBackend;
        return 0;
#endif
    }
};
#endif

#include "analyzer.h"
#include "audiobuffer/pull_buffer_stream.h"
#include "dart_callback_gate.h"
#include "device_lifecycle_test_hooks.h"
// The FlutterEngine lifecycle entry points defined below. Included so the
// declarations the embedder plugins compile against are checked against these
// definitions rather than being repeated by hand in Java, Objective-C++ and
// Dart.
#include "engine_lifecycle.h"
#include "mixeroutput/mixer_output.h"
#include "mixeroutput/wav_output_encoder.h"
#include "player.h"
#include "soloud/include/soloud_bus.h"
#include "soloud/include/soloud_fft.h"
#include "soloud/include/soloud_internal.h"
#include "soloud_thread.h"
#include "waveform/waveform.h"

#ifndef SOLOUD_COMMON_H
#include "soloud_common.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <pthread.h>
#ifdef MA_ENABLE_AUDIO_WORKLETS
#include <emscripten/threading.h>
#include <emscripten/webaudio.h>
#endif
#endif

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory.h>
#include <memory>
#include <mutex>
#include <stdio.h>
#include <thread>

#if defined(__ANDROID__)
#include <jni.h>
#endif

// Defined below with the other C-linkage globals. Declared here so the engine
// lifecycle helpers can raise it while holding engine_lifecycle_mutex.
extern "C" std::atomic<bool> engine_shutdown_requested;

namespace
{
  /// Used when no FlutterEngine lifecycle is available: the web build, the
  /// desktop embedders, and any host that does not run the Android plugin.
  /// Every lifecycle check treats it as "not an owner".
  constexpr int64_t kNoEngineId = dart_callbacks::kNoEngineId;

  /// The generation the process-global callables below were published at.
  /// Guarded by the Dart callback gate, like every other registration.
  ///
  /// Callback ownership lives in the gate and decides only whose *callables*
  /// may be retired — never whether the engine may be torn down. It is unset
  /// for the whole of an initialization, because Dart registers callbacks only
  /// after initEngine() has returned.
  uint64_t globalCallbackGeneration = dart_callbacks::kNoGeneration;

  /// The generation the mixer-output callable was published at. It gets its own
  /// because it is published separately from the other three — by
  /// startMixerOutputStream(), possibly from a worker isolate — so the
  /// registration live when it was installed is not necessarily the one live
  /// now. Judging it by globalCallbackGeneration would let a callable installed
  /// under a dead registration come back to life under a later engine's.
  uint64_t mixerCallbackGeneration = dart_callbacks::kNoGeneration;

  /// Which FlutterEngine currently claims the native engine, and a counter
  /// advanced by every prepareEngineInit(). Claimed at the *start* of an
  /// initialization rather than when callbacks register, so the engine has a
  /// lifecycle owner during the whole init — including the long window where
  /// initEngine() has opened the device but Dart has not registered callbacks
  /// yet.
  ///
  /// Both fields are read and written together, so they are guarded by a mutex
  /// rather than made individually atomic: a teardown must observe a consistent
  /// (owner, generation) pair, and no interleaving of two atomic loads gives
  /// that.
  ///
  /// Lock ordering: this is a leaf, like the Dart callback gate, and the two
  /// are never held together. It is never held across a device operation, a
  /// thread join, or any other blocking work. The rest of the file nests as
  /// `init_deinit_mutex -> loadMutex -> dart callback gate`.
  /// Serializes mixer-capture lifecycle transitions: MixerOutput::start() and
  /// stop() share buffers, encoder/queue unique_ptrs and two std::thread
  /// objects, and guard themselves with nothing but an atomic `m_running` flag
  /// — a check-then-act that two callers can both pass. Two concurrent stops
  /// then join the same thread twice; a start racing a stop rebuilds the state
  /// the stop is tearing down.
  ///
  /// That is reachable from ordinary use, not just from two engines: a capture
  /// can be started and stopped from a worker isolate
  /// (`SoLoudIsolate.startMixerOutputStream()`) while engine teardown stops the
  /// same capture from its own worker thread.
  ///
  /// It also carries the "no capture may start once teardown has been decided"
  /// check, which has to be atomic with the start itself — otherwise a start
  /// that has already passed the check can still create a capture after
  /// teardown's stop() has run, leaving a live notification thread attached to
  /// a disposed engine.
  ///
  /// Lock ordering: `init_deinit_mutex -> mixer_lifecycle_mutex`. Never the
  /// reverse — startMixerCapture() releases init_deinit_mutex before taking
  /// this. Neither is ever held while acquiring the Dart callback gate, which
  /// stays a leaf.
  std::mutex mixer_lifecycle_mutex;

  std::mutex engine_lifecycle_mutex;
  int64_t nativeInitOwnerEngineId = kNoEngineId;
  uint64_t engineInitGeneration = 0;

  /// Advanced every time a shutdown is requested, by any route.
  ///
  /// It exists for callers that cannot claim the engine synchronously. iOS has
  /// to hand the claim to its plugin over a method channel, so Dart is
  /// suspended between deciding to initialize and the claim actually being
  /// taken — and `deinit()` can run in that gap. Without this, the late claim
  /// would land *after* the teardown that superseded it, lowering the shutdown
  /// flag and leaving an ownership claim for an engine that no longer exists.
  ///
  /// Such a caller reads the epoch before it starts, and the claim is refused
  /// if the epoch has moved by the time it arrives. A synchronous caller has no
  /// gap to protect and does not need it.
  uint64_t engineShutdownEpoch = 0;

  /// Raise the shutdown flag and invalidate every prepare request that was
  /// already in flight. Callers must hold engine_lifecycle_mutex.
  void requestShutdownLocked()
  {
    engine_shutdown_requested.store(true, std::memory_order_release);
    ++engineShutdownEpoch;
  }

  /// A snapshot of the lifecycle claim, taken so a worker that will run later
  /// can tell whether the engine it was asked to act on is still the current
  /// one.
  struct EngineLifecycleClaim
  {
    int64_t ownerEngineId;
    uint64_t generation;
  };

  bool engineLifecycleClaimIsCurrent(const EngineLifecycleClaim &claim)
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    return claim.ownerEngineId != kNoEngineId &&
           nativeInitOwnerEngineId == claim.ownerEngineId &&
           engineInitGeneration == claim.generation;
  }

  /// Accept a teardown for [engine_id] and capture the claim it is tearing
  /// down.
  ///
  /// Verifying the claim and raising engine_shutdown_requested in one critical
  /// section is what stops a detaching engine cancelling a *replacement*
  /// engine's initialization. prepareEngineInit() lowers that flag and
  /// re-claims under the same mutex, so the two can no longer interleave as:
  /// teardown reads the old claim, replacement re-claims and lowers the flag,
  /// teardown raises it again, and the replacement's initEngine() then refuses
  /// to initialize.
  bool tryBeginEngineTeardown(int64_t engine_id, EngineLifecycleClaim *out)
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);

    if (engine_id == kNoEngineId || nativeInitOwnerEngineId != engine_id)
      return false;

    *out = EngineLifecycleClaim{nativeInitOwnerEngineId, engineInitGeneration};
    // Rejects an initialization worker of this same engine that has not entered
    // native code yet, and any prepare request still in flight.
    requestShutdownLocked();
    return true;
  }

  void releaseEngineLifecycleClaim()
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    nativeInitOwnerEngineId = kNoEngineId;
  }

  /// Release the claim only when it is still the one being torn down.
  ///
  /// An unconditional release lets a stale operation strip a live engine's
  /// ownership: a queued teardown worker whose engine has since been replaced
  /// would leave the replacement initialized but unowned, and an unowned engine
  /// can never be torn down when *its* FlutterEngine is destroyed.
  void releaseEngineLifecycleClaimIf(const EngineLifecycleClaim &claim)
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    if (nativeInitOwnerEngineId == claim.ownerEngineId &&
        engineInitGeneration == claim.generation)
      nativeInitOwnerEngineId = kNoEngineId;
  }
} // namespace

#ifdef __cplusplus
extern "C"
{
#endif

  /// mutex to lock the init and dispose methods.
  std::mutex init_deinit_mutex;

  /// Set by Dart as soon as a shutdown is requested, so worker scheduling
  /// cannot make a later init resurrect the engine.
  std::atomic<bool> engine_shutdown_requested{false};

  /// Lock-free readiness publication for UI-side isInitialized queries.
  std::atomic<bool> engine_initialized{false};

  /// mutex to lock the loading audio methods and make safe operations on
  /// player.sounds list.
  std::mutex loadMutex;

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
  /// Defined with the other test hooks, far below. Inert unless a test arms it.
  static void soloudTestInitBarrier();
#endif

  std::unique_ptr<Player> player = std::make_unique<Player>();
  std::unique_ptr<Analyzer> analyzer = std::make_unique<Analyzer>(256);

  typedef void (*dartVoiceEndedCallback_t)(unsigned int *);
  typedef void (*dartFileLoadedCallback_t)(enum PlayerErrors *, char *completeFileName, unsigned int *, uint64_t *counter);
  typedef void (*dartStateChangedCallback_t)(enum PlayerStateEvents *);
  // dartMixerOutputDataCallback_t comes from enums.h: ffi_gen_tmp.h has to be
  // able to name it too.

  // to be used by `NativeCallable`, these functions must return void.
  // Atomic so the audio thread can safely snapshot the pointer before calling.
  std::atomic<dartVoiceEndedCallback_t> dartVoiceEndedCallback{nullptr};
  std::atomic<dartFileLoadedCallback_t> dartFileLoadedCallback{nullptr};
  std::atomic<dartStateChangedCallback_t> dartStateChangedCallback{nullptr};
  std::atomic<dartMixerOutputDataCallback_t> dartMixerOutputDataCallback{nullptr};

  /// Monotonic engine session counter, bumped every time the native player
  /// is torn down and recreated by `dispose()`. Voice handles restart from
  /// scratch in the new Player, and on the web the voiceEnded events posted
  /// by the old engine travel asynchronously (web worker round-trip, plus a
  /// main-thread proxy hop on the AudioWorklet build), so they can arrive
  /// after the new engine has started and kill a new voice that reused the
  /// same handle id. Events are tagged with the generation of the engine
  /// that emitted them, letting the Dart side drop stale ones.
  std::atomic<unsigned int> engineGeneration{0};

  //////////////////////////////////////////////////////////////
  /// WEB WORKER

#ifdef __EMSCRIPTEN__
  /// Create the web worker and store a global "Module_soloud.workerUri" in JS.
  FFI_PLUGIN_EXPORT bool createWorkerInWasm()
  {
    printf("CPP bool createWorkerInWasm()\n");

    return EM_ASM_INT({
      // Create a new Worker from the URI
      var workerUri = "assets/packages/flutter_soloud/web/worker.dart.js";
      console.log("EM_ASM creating Web Worker!");
      try
      {
        var newWorker = new Worker(workerUri);
        // Replace the existing worker only after the new one was created,
        // so a failed (re)creation doesn't lose a working worker.
        if (Module_soloud.wasmWorker)
        {
          try
          {
            Module_soloud.wasmWorker.terminate();
            console.log("EM_ASM terminated existing Web Worker.");
          }
          catch (e)
          {
            console.error('Failed to terminate existing worker:', e);
          }
        }
        Module_soloud.wasmWorker = newWorker;
        return 1;
      }
      catch (e)
      {
        console.error('Failed to create worker:', e);
        return 0;
      }
    });
  }

  /// Posts an event message to the web event worker. MUST run on the main
  /// browser thread, which is where Module_soloud.wasmWorker lives.
  static void postMessageToEventWorker(int message, int value, int generation)
  {
    EM_ASM(
        {
          if (Module_soloud.wasmWorker)
          {
            // Send the message
            Module_soloud.wasmWorker.postMessage({
              message : UTF8ToString($0),
              value : $1,
              generation : $2,
            });
            // console.log("EM_ASM posting message " + UTF8ToString($0) +
            //     " with value " + $1);
          }
          else
          {
            console.error('flutter_soloud: the event worker is not created; dropping message "' + UTF8ToString($0) + '"');
          }
        },
        message, value, generation);
  }

  /// Posts an event message to the web event worker with an explicit engine
  /// session [generation] (see engineGeneration). Safe to call from any
  /// thread: off the main browser thread the post is routed through the
  /// AudioWorklet message port.
  static void sendToWorkerGen(const char *message, int value,
                              unsigned int generation)
  {
#ifdef MA_ENABLE_AUDIO_WORKLETS
    if (!emscripten_is_main_browser_thread())
    {
      // On the multi-threaded (AudioWorklet) build `voiceEndedCallback` fires
      // from the AudioWorklet rendering thread. MAIN_THREAD_ASYNC_EM_ASM does
      // NOT proxy to the main browser thread from there: it executes in the
      // worklet's own JS realm, where Module_soloud.wasmWorker does not exist
      // and every event would be dropped. Route the post through the
      // AudioWorklet message port instead; it runs postMessageToEventWorker
      // on the main thread.
      emscripten_audio_worklet_post_function_viii(
          EMSCRIPTEN_AUDIO_MAIN_THREAD, postMessageToEventWorker,
          (int)(uintptr_t)message, value, (int)generation);
      return;
    }
#endif
    // Main browser thread (or the single-threaded build): post directly.
    postMessageToEventWorker((int)(uintptr_t)message, value, (int)generation);
  }

  /// Post a message with the web worker.
  FFI_PLUGIN_EXPORT void sendToWorker(const char *message, int value)
  {
    // The generation is captured now, on the calling thread: delivery to the
    // main thread is asynchronous and can complete after the engine has been
    // torn down and re-initialized, and the event must stay tagged with the
    // session that produced it.
    sendToWorkerGen(message, value,
                    engineGeneration.load(std::memory_order_acquire));
  }

  /// Returns the current engine session counter (see [engineGeneration]).
  FFI_PLUGIN_EXPORT unsigned int getEngineGeneration()
  {
    return engineGeneration.load(std::memory_order_acquire);
  }

  /// Posts a mixer output notification to the web event worker. MUST run on
  /// the main browser thread, which is where Module_soloud.wasmWorker lives.
  static void postMixerOutputToEventWorker(int offset, int length,
                                           int captureId)
  {
    EM_ASM(
        {
          if (Module_soloud.wasmWorker)
          {
            Module_soloud.wasmWorker.postMessage({
              message : 'mixerOutputData',
              offset : $0,
              length : $1,
              captureId : $2,
            });
          }
        },
        offset, length, captureId);
  }

  /// Notify the web worker that new mixer output data is available.
  /// [offset] byte offset into the mixer output circular buffer.
  /// [length] number of contiguous valid bytes.
  /// [captureId] identifies the active capture session so the Dart side
  /// can discard stale notifications.
  static void sendMixerOutputToWorker(size_t offset, size_t length,
                                      uint32_t captureId)
  {
#ifdef MA_ENABLE_AUDIO_WORKLETS
    if (!emscripten_is_main_browser_thread())
    {
      // Same AudioWorklet routing as sendToWorker(): on the multi-threaded
      // build the notification runs inline in the audio callback, i.e. on the
      // AudioWorklet rendering thread (see MixerOutput::onAudioData), where
      // MAIN_THREAD_ASYNC_EM_ASM would execute in the worklet's own JS realm
      // and never reach the event worker on the main thread.
      emscripten_audio_worklet_post_function_viii(
          EMSCRIPTEN_AUDIO_MAIN_THREAD, postMixerOutputToEventWorker,
          static_cast<int>(offset), static_cast<int>(length),
          static_cast<int>(captureId));
      return;
    }
#endif
    // Main browser thread (or the single-threaded build): post directly.
    postMixerOutputToEventWorker(static_cast<int>(offset),
                                 static_cast<int>(length),
                                 static_cast<int>(captureId));
  }
#endif

  FFI_PLUGIN_EXPORT void nativeFree(void *pointer) { free(pointer); }

  /// Body of [voiceEndedCallback], factored out so the whole body can run on
  /// the main browser thread with the engine session [generation] captured
  /// on the thread that produced the event.
  static void voiceEndedBody(unsigned int *handle, unsigned int generation)
  {
    if (player != nullptr)
    {
      player->removeHandle(*handle);
    }

#ifdef __EMSCRIPTEN__
    sendToWorkerGen("voiceEndedCallback", *handle, generation);
#endif

    // The `dartVoiceEndedCallback` is not set on Web.
    // Held across the call, not just the load: a retirement running
    // concurrently must not return while a trampoline is still executing,
    // because the isolate that owns it is about to go away.
    const dart_callbacks::InvocationPass pass;
    if (!pass.isLive(globalCallbackGeneration))
      return;
    auto voiceEndedCb = dartVoiceEndedCallback.load(std::memory_order_acquire);
    if (voiceEndedCb == nullptr)
      return;
    // [n] pointer must be deleted in Dart.
    unsigned int *n = (unsigned int *)malloc(sizeof(unsigned int));
    *n = *handle;
    voiceEndedCb(n);
  }

#if defined(__EMSCRIPTEN__) && defined(MA_ENABLE_AUDIO_WORKLETS)
  /// Entry point posted from the AudioWorklet rendering thread to the main
  /// browser thread (see voiceEndedCallback).
  static void voiceEndedForwardToMain(int handle, int generation)
  {
    unsigned int h = (unsigned int)handle;
    voiceEndedBody(&h, (unsigned int)generation);
  }
#endif

  /// The callback to monitor when a voice ends.
  ///
  /// It is called by void `Soloud::stopVoice_internal(unsigned int aVoice)` when
  /// a voice ends and comes from the audio thread (so on the web, from a
  /// different web worker).
  FFI_PLUGIN_EXPORT void voiceEndedCallback(unsigned int *handle)
  {
#if defined(__EMSCRIPTEN__) && defined(MA_ENABLE_AUDIO_WORKLETS)
    if (!emscripten_is_main_browser_thread())
    {
      // On the AudioWorklet rendering thread the body would take the player's
      // sounds_mutex (findByHandle/removeHandle); a contended lock on this
      // thread lowers to a futex wait, which Emscripten aborts on (futex
      // waits are illegal on AudioWorklet threads). Do everything on the
      // main browser thread instead. The generation must be captured now:
      // the engine may be re-initialized before the main thread runs the
      // body, and the event must stay tagged with the session that produced
      // it.
      const unsigned int h = *handle;
      const unsigned int generation =
          engineGeneration.load(std::memory_order_acquire);
      emscripten_audio_worklet_post_function_vii(
          EMSCRIPTEN_AUDIO_MAIN_THREAD, voiceEndedForwardToMain,
          (int)h, (int)generation);
      return;
    }
#endif
    voiceEndedBody(handle, engineGeneration.load(std::memory_order_acquire));
  }
  
  /// Requests a device-idle evaluation after SoLoud stops or pauses a voice.
  /// SoLoud invokes this only after releasing its audio mutex.
  FFI_PLUGIN_EXPORT void voiceInactiveCallback()
  {
    if (player != nullptr)
      player->evaluateAudioDeviceIdle();
  }

  /// The callback to monitor when a file is loaded.
  void fileLoadedCallback(enum PlayerErrors error, char *completeFileName, unsigned int *hash, uint64_t counter)
  {
    const dart_callbacks::InvocationPass pass;
    if (!pass.isLive(globalCallbackGeneration))
      return;
    auto fileLoadedCb = dartFileLoadedCallback.load(std::memory_order_acquire);
    if (fileLoadedCb == nullptr)
      return;
    // [e,name,n] pointers must be deleted on Dart.
    PlayerErrors *e = (PlayerErrors *)malloc(sizeof(PlayerErrors));
    *e = error;
    char *name = strdup(completeFileName);
    unsigned int *n = (unsigned int *)malloc(sizeof(unsigned int));
    *n = *hash;
    uint64_t *ts = (uint64_t *)malloc(sizeof(uint64_t));
    *ts = counter;
    fileLoadedCb(e, name, n, ts);
  }

  void stateChangedCallback(unsigned int state)
  {
    const dart_callbacks::InvocationPass pass;
    if (!pass.isLive(globalCallbackGeneration))
      return;
    auto stateChangedCb = dartStateChangedCallback.load(std::memory_order_acquire);
    if (stateChangedCb == nullptr)
      return;
    PlayerStateEvents *type = (PlayerStateEvents *)malloc(sizeof(PlayerStateEvents));
    *type = (PlayerStateEvents)state;
    stateChangedCb(type);
  }

  /// Set a Dart functions to call when an event occurs.
  ///
  /// [owner_engine_id] is the FlutterEngine whose isolate created these
  /// trampolines, or -1 where no engine lifecycle is available. It is recorded
  /// so that a detaching or hot-restarting engine can retire *its own*
  /// callables and never somebody else's.
  FFI_PLUGIN_EXPORT void
  setDartEventCallback(dartVoiceEndedCallback_t voice_ended_callback,
                       dartFileLoadedCallback_t file_loaded_callback,
                       dartStateChangedCallback_t state_changed_callback,
                       int64_t owner_engine_id)
  {
    dart_callbacks::Registration registration;
    dartVoiceEndedCallback.store(voice_ended_callback,
                                 std::memory_order_release);
    dartFileLoadedCallback.store(file_loaded_callback,
                                 std::memory_order_release);
    dartStateChangedCallback.store(state_changed_callback,
                                   std::memory_order_release);
    // Claiming here retires every earlier registration, this engine's included,
    // and every source that recorded an earlier generation.
    globalCallbackGeneration = registration.claim(owner_engine_id);
  }

  /// Null the process-global callable pointers. Ownership and liveness live in
  /// the gate; this is hygiene, so a stale pointer cannot be read back after
  /// the callables it names have been closed on the Dart side.
  ///
  /// The caller must hold the gate exclusively.
  static void clearDartCallbackPointersLocked()
  {
    dartVoiceEndedCallback.store(nullptr, std::memory_order_release);
    dartFileLoadedCallback.store(nullptr, std::memory_order_release);
    dartStateChangedCallback.store(nullptr, std::memory_order_release);
    dartMixerOutputDataCallback.store(nullptr, std::memory_order_release);
    globalCallbackGeneration = dart_callbacks::kNoGeneration;
    mixerCallbackGeneration = dart_callbacks::kNoGeneration;
  }

  /// Additionally null the Dart callbacks stored inside Player-owned state: the
  /// voice-ended/state-changed hooks and the per-BufferStream and
  /// per-PullBufferStream callbacks.
  ///
  /// This is hygiene too, and it is *not* what makes those callbacks inert —
  /// retiring the gate generation already did that, everywhere, without
  /// touching a single source. That matters because reaching them takes
  /// `sounds_mutex`, which addAudioDataStream() holds across decoding, so this
  /// can block for as long as a decode: it must never run on a thread that
  /// cannot wait, and in particular never on Android's platform thread.
  ///
  /// The caller must hold init_deinit_mutex, which owns the `player`
  /// unique_ptr that dispose() resets, and must *not* hold the gate.
  static void clearPlayerDartCallbackRegistrationsLocked()
  {
    if (player.get() != nullptr)
    {
      player.get()->clearDartCallbackRegistrations();
    }
  }

  FFI_PLUGIN_EXPORT void clearDartCallbackRegistrations()
  {
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);

    {
      dart_callbacks::Registration registration;
      registration.retireAll();
      clearDartCallbackPointersLocked();
    }

    // Outside the gate: stop() joins the encoder and notification threads, and
    // those threads take the gate to invoke the mixer-output callable.
    {
      std::lock_guard<std::mutex> mixerGuard(mixer_lifecycle_mutex);
      MixerOutput::instance().setDataCallback(nullptr);
      MixerOutput::instance().stop();
    }
    clearPlayerDartCallbackRegistrationsLocked();
  }

  /// Retire every Dart callable owned by [engine_id] because its isolate is
  /// going away (a hot restart, or its FlutterEngine being destroyed). Returns
  /// false when a different engine owns the live registration, so a detaching
  /// engine never retires another one's callables.
  ///
  /// This runs on the Android platform (UI) thread, so it takes exactly one
  /// lock — the gate — and holds it across a handful of stores. It waits for no
  /// device operation, no decode, no `sounds_mutex`, no `init_deinit_mutex`
  /// (held for the whole of dispose(), which joins the lifecycle scheduler and
  /// can inherit a stalled ma_device_stop()), and it spawns nothing. When it
  /// returns, every callable in the process — the four global ones and every
  /// per-source one — is inert and no invocation is in flight.
  FFI_PLUGIN_EXPORT bool clearDartCallbackRegistrationsForEngine(
      int64_t engine_id)
  {
    if (engine_id == kNoEngineId)
      return false;

    dart_callbacks::Registration registration;
    if (!registration.retire(engine_id))
      return false;

    clearDartCallbackPointersLocked();
    return true;
  }

  // Mixer output capture exports.

  FFI_PLUGIN_EXPORT enum PlayerErrors startMixerCapture(
      int format, int sampleRate, int channels,
      int bufferSizeBytes,
      int notificationThresholdBytes,
      int chunkPCMFrames)
  {
    MixerOutputFormat outputFormat = static_cast<MixerOutputFormat>(format);

    int sr = sampleRate;
    int ch = channels;
    if (sr <= 0 || ch <= 0)
    {
      // Scoped deliberately: init_deinit_mutex must be released before
      // mixer_lifecycle_mutex is taken, or this inverts the order that
      // disposeLocked() uses and the two deadlock.
      std::lock_guard<std::mutex> guard(init_deinit_mutex);
      if (player.get() != nullptr && player.get()->isInited())
      {
        if (sr <= 0)
          sr = static_cast<int>(player.get()->soloud.getBackendSamplerate());
        if (ch <= 0)
          ch = static_cast<int>(player.get()->soloud.getBackendChannels());
      }
    }

    if (sr <= 0)
      sr = 44100;
    if (ch <= 0)
      ch = 2;

    std::lock_guard<std::mutex> mixerGuard(mixer_lifecycle_mutex);

    // Checked here rather than by the caller, and under the same lock as the
    // start, because Dart's readiness check always races the native transition:
    // requestEngineTeardownForEngine() raises this flag synchronously on the
    // platform thread, long before its worker gets to stop the mixer, so a
    // capture that begins after that point would otherwise outlive the engine
    // it belongs to. Ordering is now decided by this mutex: a start that gets
    // here first runs and is stopped by the teardown; one that arrives after is
    // refused.
    if (engine_shutdown_requested.load(std::memory_order_acquire))
      return backendNotInited;

    return MixerOutput::instance().start(
        outputFormat, sr, ch,
        static_cast<size_t>(bufferSizeBytes),
        static_cast<size_t>(notificationThresholdBytes),
        chunkPCMFrames);
  }

  FFI_PLUGIN_EXPORT void stopMixerCapture()
  {
    // Serialized against engine teardown's own stop, and against another
    // isolate's: MixerOutput::stop() joins both worker threads and resets the
    // encoder and queue, none of which survives being run twice at once.
    std::lock_guard<std::mutex> mixerGuard(mixer_lifecycle_mutex);
#ifdef __EMSCRIPTEN__
    // On the multi-threaded (AudioWorklet) build the audio callback runs on
    // the worklet rendering thread and encodes inline in onAudioData().
    // Synchronize with it before finalizing/freeing the encoder, otherwise
    // stop() can race an in-flight encode (use-after-free / OOB). The main
    // thread may block on the mutex (ALLOW_BLOCKING_ON_MAIN_THREAD); the
    // worklet side never blocks — it try-locks and emits silence on
    // contention (see soloud_miniaudio_audiomixer).
    if (player.get() != nullptr && player.get()->isInited())
    {
      player.get()->soloud.lockAudioMutex_internal();
      MixerOutput::instance().stop();
      player.get()->soloud.unlockAudioMutex_internal();
      return;
    }
#endif
    MixerOutput::instance().stop();
  }

  FFI_PLUGIN_EXPORT int isMixerCaptureRunning()
  {
    return MixerOutput::instance().isRunning() ? 1 : 0;
  }

  FFI_PLUGIN_EXPORT unsigned char *getMixerCaptureBufferPointer()
  {
    return MixerOutput::instance().getBufferPointer();
  }

  FFI_PLUGIN_EXPORT int getMixerCaptureBufferSize()
  {
    return static_cast<int>(MixerOutput::instance().getBufferSize());
  }

  FFI_PLUGIN_EXPORT int getMixerCaptureAvailableBytes()
  {
    return static_cast<int>(MixerOutput::instance().getAvailableBytes());
  }

  FFI_PLUGIN_EXPORT int getMixerCaptureReadOffset()
  {
    return static_cast<int>(MixerOutput::instance().getReadOffset());
  }

  FFI_PLUGIN_EXPORT void advanceMixerCaptureReadPosition(
      int bytes)
  {
    MixerOutput::instance().advanceReadPosition(
        static_cast<size_t>(bytes));
  }

  FFI_PLUGIN_EXPORT unsigned char *getMixerOutputWavHeader()
  {
    const auto &header = MixerOutput::instance().getWavHeader();
    if (header.empty())
    {
      return nullptr;
    }

    auto *result = static_cast<unsigned char *>(malloc(header.size()));
    if (result == nullptr)
    {
      return nullptr;
    }
    std::memcpy(result, header.data(), header.size());
    return result;
  }

  /// What MixerOutput calls on its notification thread. Named rather than a
  /// lambda so a test can drive the exact function MixerOutput holds.
  static void dispatchMixerOutputToDart(uint8_t *data, size_t length)
  {
#ifdef __EMSCRIPTEN__
    // On the web the callback may fire from the audio thread, so we cannot call
    // Dart directly. Send the offset/length to the web worker, which forwards
    // it to the main isolate.
    if (length == 0)
      return;
    const size_t offset = reinterpret_cast<size_t>(data);
    sendMixerOutputToWorker(offset, length,
                            MixerOutput::instance().captureId());
#else
    // Held across the call so a retirement cannot return — and the owning
    // isolate cannot go away — while this trampoline is running. Checked
    // against the generation *this callable* was published at, not the one
    // currently live: those differ whenever the mixer callable outlives the
    // registration it joined.
    const dart_callbacks::InvocationPass pass;
    if (!pass.isLive(mixerCallbackGeneration))
      return;
    auto cb = dartMixerOutputDataCallback.load(std::memory_order_acquire);
    if (cb != nullptr)
    {
      cb(data, static_cast<uint64_t>(length));
    }
#endif
  }

  /// Publish the mixer-output callable and tag it with the registration it
  /// joins. [require_live] is false only for the lifecycle-free entry point
  /// below.
  static bool publishMixerOutputCallback(dartMixerOutputDataCallback_t callback,
                                         int64_t owner_engine_id,
                                         bool require_live)
  {
    {
      dart_callbacks::Registration registration;

      if (owner_engine_id != kNoEngineId)
      {
        // A caller that can name its engine must own the live registration.
        if (!registration.isOwnedBy(owner_engine_id))
          return false;
      }
      else if (require_live &&
               registration.generation() == dart_callbacks::kNoGeneration)
      {
        // A caller that cannot name its engine may still only join a
        // registration that is actually live.
        return false;
      }

      dartMixerOutputDataCallback.store(callback, std::memory_order_release);
      mixerCallbackGeneration = registration.generation();
    }

    MixerOutput::instance().setDataCallback(dispatchMixerOutputToDart);
    return true;
  }

  /// Publish the mixer-output data callable for [owner_engine_id].
  ///
  /// Unlike the other three this one is published on its own, and re-published
  /// whenever mixer capture starts, so it cannot lean on setDartEventCallback()
  /// having just recorded the owner. It joins the live registration instead and
  /// carries that generation, so it dies with the registration it joined and
  /// cannot be revived by a later engine claiming a new one.
  ///
  /// [owner_engine_id] of -1 means "I cannot name my engine", not "there is no
  /// engine": a worker isolate reaches this through
  /// `SoLoudIsolate.startMixerOutputStream()`, and `PlatformDispatcher.engineId`
  /// is only set on the isolate the engine runs. Such a caller still may not
  /// publish into a retired registration — that is how a capture isolate that
  /// is still running while its FlutterEngine is being destroyed is stopped
  /// from re-arming a callable retirement has just made inert.
  ///
  /// What it cannot tell apart is a worker of engine A publishing while a
  /// *replacement* engine B holds the live registration; it would join B's.
  /// Closing that needs the worker to name its engine, which needs the id
  /// plumbed through the isolate that spawned it, and it only arises with
  /// overlapping FlutterEngines — which this package does not support.
  ///
  /// Returns whether the callable was published.
  FFI_PLUGIN_EXPORT bool setMixerOutputCallbackForEngine(
      dartMixerOutputDataCallback_t callback, int64_t owner_engine_id)
  {
    return publishMixerOutputCallback(callback, owner_engine_id,
                                      /*require_live=*/true);
  }

  /// Lifecycle-free entry point, for hosts where no registration is ever
  /// claimed. The web build binds this exported symbol directly from the
  /// prebuilt wasm, so its signature must not change — and it must keep
  /// publishing unconditionally, because the web never claims a generation and
  /// its dispatch does not consult one.
  FFI_PLUGIN_EXPORT void setMixerOutputCallback(
      dartMixerOutputDataCallback_t callback)
  {
    publishMixerOutputCallback(callback, kNoEngineId, /*require_live=*/false);
  }

  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////
  //////////////////////////////////////////////////////////////////////////////////

  /// Check if the libopus and libogg are available at build time.
  FFI_PLUGIN_EXPORT bool areXiphLibsAvailable()
  {
#if !defined(NO_XIPH_LIBS)
    return true;
#else
  return false;
#endif
  }

  /// Initialize the player. Must be called before any other player functions.
  ///
  /// [sampleRate] the sample rate. Usually is 22050, 44100 (CD quality) or 48000.
  /// [bufferSize] the audio buffer size. Usually is 2048, but can be also 512
  /// when low latency is needed for example in games. [channels] 1=mono,
  /// 2=stereo, 4=quad, 6=5.1, 8=7.1.
  /// [devicePeriodFrames] small output device period used when
  /// [renderAheadFrames] enables the render-ahead ring; 0 = default (512).
  /// [renderAheadFrames] depth of the engine-owned render-ahead ring in
  /// frames; 0 disables it and keeps direct-to-device mixing.
  ///
  /// Returns [PlayerErrors.noError] if success.
  FFI_PLUGIN_EXPORT enum PlayerErrors initEngine(int deviceID,
                                                 unsigned int sampleRate,
                                                 unsigned int bufferSize,
                                                 unsigned int channels,
                                                 unsigned int lowLatency,
                                                 unsigned int devicePeriodFrames,
                                                 unsigned int renderAheadFrames)
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);

    if (engine_shutdown_requested.load(std::memory_order_acquire))
    {
      engine_initialized.store(false, std::memory_order_release);
      return backendNotInited;
    }

    if (player.get() == nullptr)
      player = std::make_unique<Player>();

    player.get()->setStateChangedCallback(stateChangedCallback);
    PlayerErrors res = (PlayerErrors)player.get()->init(
        sampleRate, bufferSize, channels, deviceID, lowLatency != 0,
        devicePeriodFrames, renderAheadFrames);
    if (res != noError)
    {
      engine_initialized.store(false, std::memory_order_release);
      return res;
    }

    // Set window size for filters
    const int windowSize = (player.get()->soloud.getBackendBufferSize() /
                            player.get()->soloud.getBackendChannels()) -
                           1;
    analyzer.get()->setWindowsSize(windowSize);

    // Set the callback for when a voice is ended/stopped
    player.get()->setVoiceEndedCallback(voiceEndedCallback);
    player.get()->setVoiceInactiveCallback(voiceInactiveCallback);

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
    soloudTestInitBarrier();
#endif

    if (engine_shutdown_requested.load(std::memory_order_acquire))
    {
      engine_initialized.store(false, std::memory_order_release);
      return backendNotInited;
    }

    engine_initialized.store(true, std::memory_order_release);

    return PlayerErrors::noError;
  }

  /// Android only: choose whether SoLoud tags the AAudio stream's
  /// usage/contentType (media/music) or leaves them unset so the app can manage
  /// AudioAttributes externally (e.g. via the audio_session plugin). Only takes
  /// effect with low-latency disabled. Call before initEngine(). No effect on
  /// other backends. [managed] != 0 → media/music (default); 0 → leave unset.
  FFI_PLUGIN_EXPORT void setAndroidAAudioAttributes(unsigned int managed)
  {
    SoLoud::miniaudio_setAndroidAAudioAttributes(managed != 0);
  }

  /// Set how long the audio output device keeps running while the engine is
  /// idle (no active voices) before it is automatically stopped, on every
  /// platform. [timeoutMs] < 0 keeps the device running indefinitely while idle
  /// (the deferred idle-pause is suppressed, so the device keeps rendering
  /// silence and the app keeps its OS audio session alive) and starts it
  /// immediately if it was stopped. [timeoutMs] == 0 stops the device as soon
  /// as possible once idle. [timeoutMs] > 0 keeps it running for that many
  /// milliseconds after going idle. Any play/unpause before the deadline
  /// cancels the pending stop. The default is 500. Can be called any time.
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
  /// Incremented once per completed FlutterEngine-owned teardown worker. Only
  /// the tests use it; the production API deliberately does not await that
  /// worker.
  std::atomic<int> engine_teardown_completed{0};
#endif

  /// Coalesces deferred applications of the idle-timeout policy. Every worker
  /// re-reads the published value, so one pending worker is enough no matter
  /// how many times the setter is called while the lifecycle mutex is busy.
  /// Bookkeeping for the single deferred idle-timeout worker.
  ///
  /// Guarded by its own tiny mutex, never by init_deinit_mutex: the setter runs
  /// on the UI isolate and must not wait behind a device operation. Nothing
  /// blocking happens under it.
  std::mutex idle_timeout_worker_mutex;
  /// Bumped by every publication. The worker compares it before going idle, so
  /// a value published while the worker was applying the previous one cannot be
  /// missed.
  uint64_t idle_timeout_publication = 0;
  bool idle_timeout_worker_running = false;

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
  /// Peak number of deferred workers alive at once. The whole point of the
  /// bookkeeping above is that this never exceeds 1.
  std::atomic<int> idle_timeout_worker_live{0};
  std::atomic<int> idle_timeout_worker_peak{0};
#endif

  /// Apply the published idle-timeout policy once init_deinit_mutex becomes
  /// available, without making the caller wait for it.
  ///
  /// Exactly one worker exists at a time and it always applies the newest
  /// published value. The naive alternatives both fail: clearing a "queued"
  /// flag before blocking lets every setter call spawn its own waiter, so a
  /// slow init collects a pile of threads; clearing it after applying instead
  /// drops a value published in the gap between the apply and the flag store.
  static void queueAudioDeviceIdleTimeoutApply()
  {
    {
      std::lock_guard<std::mutex> guard(idle_timeout_worker_mutex);
      ++idle_timeout_publication;
      if (idle_timeout_worker_running)
        return; // The running worker will observe the newer publication.
      idle_timeout_worker_running = true;
    }

    try
    {
      std::thread([]()
                  {
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
        const int live = idle_timeout_worker_live.fetch_add(
                             1, std::memory_order_acq_rel) + 1;
        int peak = idle_timeout_worker_peak.load(std::memory_order_acquire);
        while (live > peak &&
               !idle_timeout_worker_peak.compare_exchange_weak(
                   peak, live, std::memory_order_acq_rel))
        {
        }
#endif
        for (;;)
        {
          uint64_t applied;
          {
            std::lock_guard<std::mutex> guard(idle_timeout_worker_mutex);
            applied = idle_timeout_publication;
          }

          {
            std::lock_guard<std::mutex> guard(init_deinit_mutex);
            if (player.get() != nullptr)
              player.get()->applyPublishedAudioDeviceIdleTimeout();
          }

          SOLOUD_TEST_BARRIER(idleTimeoutWorkerApplied);

          std::lock_guard<std::mutex> guard(idle_timeout_worker_mutex);
          if (idle_timeout_publication == applied)
          {
            // Going idle and observing "nothing newer" happen under the same
            // lock the setter increments under, so a publication either sees
            // this worker still running (and is picked up by the loop above)
            // or starts a fresh one. It cannot fall between the two.
            idle_timeout_worker_running = false;
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
            idle_timeout_worker_live.fetch_sub(1, std::memory_order_acq_rel);
#endif
            return;
          }
        } })
          .detach();
    }
    catch (...)
    {
      std::lock_guard<std::mutex> guard(idle_timeout_worker_mutex);
      idle_timeout_worker_running = false;
      // Best effort. The policy is already published process-wide, so the next
      // init() still observes it.
    }
  }

  /// Set the idle-timeout policy. This is called synchronously from the UI
  /// isolate and is documented as callable at any time, so it must not block.
  FFI_PLUGIN_EXPORT void setAudioDeviceIdleTimeout(int64_t timeoutMs)
  {
    // Publish first, with no lock at all. The policy is process-global and
    // outlives any individual Player, so this alone guarantees the next init()
    // uses it even when no Player can consume the update right now.
    Player::publishAudioDeviceIdleTimeout(timeoutMs);

    // Applying it to the *current* Player needs that pointer pinned, which
    // means init_deinit_mutex -- and initEngine() holds that across the entire
    // native device open, which is seconds on Android and is exactly the stall
    // #481 moved off the UI isolate. Waiting for it here would put that stall
    // straight back on the UI thread, so take the lock only if it is free...
    {
      std::unique_lock<std::mutex> guard(init_deinit_mutex, std::try_to_lock);
      if (guard.owns_lock())
      {
        if (player.get() != nullptr)
          player.get()->applyPublishedAudioDeviceIdleTimeout();
        return;
      }
    }

    // ...and otherwise hand it to a thread that can afford to wait.
    queueAudioDeviceIdleTimeoutApply();
  }

  /// Stop the audio output device without deinitializing the engine. By default
  /// this is a successful no-op while voices are active. [force] stops the
  /// device even during active playback without mutating any voice.
  FFI_PLUGIN_EXPORT enum PlayerErrors stopAudioDevice(unsigned int force)
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    if (player.get() == nullptr)
      return backendNotInited;

    return player.get()->stopAudioDevice(force != 0);
  }

  /// Restart the audio output device previously stopped by stopAudioDevice(),
  /// so existing voices and loaded sounds keep operating. Idempotent: a no-op
  /// if the device is already started.
  FFI_PLUGIN_EXPORT enum PlayerErrors startAudioDevice()
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    if (player.get() == nullptr)
      return backendNotInited;

    return player.get()->startAudioDevice();
  }

  /// Get the current state of the audio output device. Returns
  /// [AudioDeviceState.audioDeviceUninitialized] if the engine is not
  /// initialized.
  FFI_PLUGIN_EXPORT enum AudioDeviceState getAudioDeviceState()
  {
    // Read the process-global backend state directly so this cheap synchronous
    // query never waits behind an initialization or lifecycle API call.
    return (AudioDeviceState)SoLoud::miniaudio_getAudioDeviceState();
  }

  /// Test-only hook that sends an interruption through miniaudio's normal
  /// notification callback. This is intentionally absent from the public API.
  FFI_PLUGIN_EXPORT void debugTriggerAudioInterruption(unsigned int began)
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    SoLoud::miniaudio_debugTriggerAudioInterruption(began != 0);
  }

  /// List playback devices.
  FFI_PLUGIN_EXPORT void listPlaybackDevices(char **devicesName, int **deviceId,
                                             int **isDefault, int *n_devices)
  {
    // Deliberately not routed through the global `player`: `dispose()` now runs
    // on a worker isolate, so reading that `unique_ptr` here would race with it
    // being reset and replaced. Enumeration needs no player state, so call it
    // directly instead of guarding with the lifecycle mutex — taking that lock
    // would stall this (UI-thread) call for the whole of an in-flight
    // `initEngine()`, which is the very block this release removes.
    std::vector<PlaybackDevice> d = Player::listPlaybackDevices();

    int numDevices = 0;
    for (int i = 0; i < (int)d.size(); i++)
    {
      /// Check the length first: the character scan below reads the first 5
      /// bytes, which is only in bounds once the name is known to be longer.
      if (d[i].name.size() <= 5)
        continue;

      bool hasSpecialChar = false;
      /// check if the device name has some strange chars (happens on Linux)
      /// It happens that some results had the name composed of non-text
      /// ASCII characters with values <0x20 (blank space) which cannot be
      /// real devices and should be ignored. Doesn't happen on my Linux
      /// anymore (maybe was a bug on audio drivers?), but worth checking
      /// to be sure.
      for (int n = 0; n < 5; n++)
      {
        if (d[i].name[n] < 0x20 && d[i].name[n] >= 0)
          hasSpecialChar = true;
      }
      if (hasSpecialChar)
        continue;

      devicesName[numDevices] = strdup(d[i].name.c_str());
      isDefault[numDevices] = (int *)malloc(sizeof(int));
      *isDefault[numDevices] = d[i].isDefault;
      deviceId[numDevices] = (int *)malloc(sizeof(int));
      *deviceId[numDevices] = d[i].id;

      numDevices++;
    }
    *n_devices = numDevices;
  }

  /// Change the playback device.
  ///
  /// [deviceID] the device ID. -1 for default OS output device.
  FFI_PLUGIN_EXPORT enum PlayerErrors changeDevice(int deviceID)
  {
    // Pins the global `player` for the whole operation, exactly as the
    // start/stop exports do. Without it a change worker can be inside
    // Player::changeDevice() while a teardown worker disposes that Player and
    // installs a replacement -- the change then runs on freed memory. The
    // window is wide on this path because device enumeration deliberately
    // happens before Player::mDeviceLifecycleOperationMutex is taken.
    //
    // Holding init_deinit_mutex across enumeration and device replacement is
    // affordable only because Dart runs changeDevice() on a worker isolate, so
    // the blocking is never on Flutter's UI isolate.
    //
    // Lock order. init_deinit_mutex is strictly outermost -- no Player member
    // function acquires it -- and below that it is a partial order, not a
    // chain:
    //
    //   init_deinit_mutex
    //     -> Player::mDeviceLifecycleOperationMutex
    //          -> Player::mInterruptionMutex -> Player::mPauseMutex
    //          -> SoLoud::gDeviceOperationMutex        (backend device call)
    //
    //   SoLoud::gDeviceOperationMutex
    //     -> Player::mInterruptionMutex -> Player::mPauseMutex
    //          (an OS interruption notification, which miniaudio can deliver
    //           inline from a backend device call)
    //
    // gDeviceOperationMutex and mInterruptionMutex are therefore both reachable
    // from the operation mutex, but never in opposite orders: no Player mutex
    // is ever held across a backend device call. mPauseMutex is a leaf --
    // nothing blocking runs under it.
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    if (player.get() == nullptr)
      return backendNotInited;

    SOLOUD_TEST_BARRIER(changeDeviceEntered);

    return player.get()->changeDevice(deviceID);
  }

  /// Free the list of playback devices.
  FFI_PLUGIN_EXPORT void freeListPlaybackDevices(char **devicesName,
                                                 int **deviceId, int **isDefault,
                                                 int n_devices)
  {
    for (int i = 0; i < n_devices; i++)
    {
      free(deviceId[i]);
      free(isDefault[i]);
      free(devicesName[i]);
    }
  }

  /// Teardown body. The caller must hold init_deinit_mutex and loadMutex.
  ///
  /// [ownedClaim] is the claim this teardown is entitled to retire, or nullptr
  /// for an unscoped teardown that retires whatever claim is current. A scoped
  /// caller must not strip a claim that has moved on: doing so would leave the
  /// engine that took it initialized but unowned, and an unowned engine can
  /// never be torn down when its own FlutterEngine is destroyed.
  static void disposeLocked(const EngineLifecycleClaim *ownedClaim)
  {
    // An in-flight init may have published readiness before releasing the
    // lifecycle lock. Reassert shutdown after acquiring it.
    engine_initialized.store(false, std::memory_order_release);

    // Make every callable inert first, waiting out any invocation currently
    // executing. The gate is not retained past this point: what follows stops
    // devices, joins threads and destroys sources.
    {
      dart_callbacks::Registration registration;
      registration.retireAll();
      clearDartCallbackPointersLocked();
    }
    {
      std::lock_guard<std::mutex> mixerGuard(mixer_lifecycle_mutex);
      MixerOutput::instance().setDataCallback(nullptr);
      MixerOutput::instance().stop();
    }

    // Nothing is left for a FlutterEngine to own. A detach arriving after this
    // finds no claim and correctly declines to tear anything down; the next
    // prepareEngineInit() takes a fresh claim.
    if (ownedClaim == nullptr)
      releaseEngineLifecycleClaim();
    else
      releaseEngineLifecycleClaimIf(*ownedClaim);

    if (player.get() == nullptr)
      return;

    clearPlayerDartCallbackRegistrationsLocked();
    // Deliberately NOT disposeAllSound(): that is a runtime operation which
    // honours the configured idle policy, so with an indefinite keep-alive it
    // ends by queueing a device *start*. Running it here would have teardown
    // ask the scheduler to start the device microseconds before joining that
    // same scheduler -- an entirely pointless ma_device_start() that deinit()
    // then has to wait out, on exactly the backends where starting is slow.
    // Player::dispose() destroys the sounds itself, as the sole owner of native
    // sound destruction during teardown.
    player.get()->dispose();
    player.reset();
    player = std::make_unique<Player>();
    // Bump the engine session counter only now that the old engine is fully
    // torn down: voiceEnded events posted while it was stopping its voices
    // keep the old generation, and the Dart side (which reads the counter
    // after the next successful initEngine) can drop them as stale.
    engineGeneration.fetch_add(1, std::memory_order_acq_rel);
    analyzer.reset();
    analyzer = std::make_unique<Analyzer>(256);
  }

  /// Must be called when there is no more need of the player or when closing
  /// the app.
  ///
  /// Ownership-unaware: it retires whatever claim is current, which is right
  /// for a deliberate Dart deinit. The FlutterEngine-scoped teardown below is
  /// what a *destroyed* engine uses.
  FFI_PLUGIN_EXPORT void dispose()
  {
    {
      std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
      requestShutdownLocked();
    }
    engine_initialized.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);

    disposeLocked(nullptr);
  }

  /// Claim the native engine for a FlutterEngine ahead of initializing it.
  ///
  /// [owner_engine_id] is the FlutterEngine that will own the engine this
  /// initialization creates, or -1 on platforms with no engine-lifecycle hooks.
  ///
  /// Called synchronously by Dart *before* it dispatches the initialization
  /// worker. Ownership must not wait for callback registration: initEngine()
  /// opens the audio device and can take seconds on Android, and Dart registers
  /// callbacks only after it returns. An engine destroyed during that window
  /// still has to be able to tear down what it just built.
  FFI_PLUGIN_EXPORT void prepareEngineInit(int64_t owner_engine_id)
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    // Lowered under the same mutex that publishes the claim, so a teardown for
    // the previous engine cannot raise it again after this point. See
    // tryBeginEngineTeardown().
    engine_shutdown_requested.store(false, std::memory_order_release);
    engine_initialized.store(false, std::memory_order_release);
    nativeInitOwnerEngineId = owner_engine_id;
    // Invalidate any teardown queued by a previous engine's detach so it cannot
    // dispose the engine this initialization is about to create.
    ++engineInitGeneration;
  }

  FFI_PLUGIN_EXPORT void requestEngineShutdown()
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    requestShutdownLocked();
  }

  /// The epoch a prepare request must quote to be accepted.
  ///
  /// Read it synchronously, before starting a claim that cannot complete
  /// synchronously; pass it to prepareEngineInitForRequest() when the claim
  /// finally happens. Anything that requests a shutdown in between moves the
  /// epoch and the claim is refused.
  FFI_PLUGIN_EXPORT uint64_t currentEngineShutdownEpoch()
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);
    return engineShutdownEpoch;
  }

  /// prepareEngineInit() for a claim that was decided earlier than it is taken.
  ///
  /// Returns false, changing nothing, when a shutdown has been requested since
  /// [shutdown_epoch] was read — the initialization that asked for this claim
  /// has been superseded, and letting it land would lower the shutdown flag and
  /// leave an ownership claim behind a teardown that already won.
  ///
  /// This is not a second way to claim the engine: it is the same claim, taken
  /// under the condition that nothing has cancelled it. A caller that can claim
  /// synchronously has no such window and should keep using prepareEngineInit().
  FFI_PLUGIN_EXPORT bool prepareEngineInitForRequest(int64_t owner_engine_id,
                                                     uint64_t shutdown_epoch)
  {
    std::lock_guard<std::mutex> guard(engine_lifecycle_mutex);

    if (engineShutdownEpoch != shutdown_epoch)
      return false;

    engine_shutdown_requested.store(false, std::memory_order_release);
    engine_initialized.store(false, std::memory_order_release);
    nativeInitOwnerEngineId = owner_engine_id;
    ++engineInitGeneration;
    return true;
  }

  /// Tear the engine down because its owning FlutterEngine is being destroyed
  /// while the process keeps running (the audio_service / add-to-app case).
  ///
  /// Without this the native engine stays initialized with a live output device
  /// and a running scheduler after the last Dart code that could drive it is
  /// gone. Returns false unless [engine_id] still owns the native engine.
  ///
  /// Ownership here is the *lifecycle* claim taken by prepareEngineInit(), not
  /// dartCallbackOwnerEngineId. Gating on callback ownership would be wrong in
  /// both directions: it is unset for the whole of an initialization — so an
  /// engine destroyed after initEngine() opened the device but before Dart
  /// registered callbacks could not tear down what it had just built — and it
  /// still names the *previous* engine once a replacement has called
  /// prepareEngineInit(), so a detaching engine would be accepted and would
  /// then capture the replacement's already-bumped generation and dispose a
  /// live engine. The generation cannot rescue either case: it is bumped when
  /// an initialization starts, which is before this teardown reads it.
  ///
  /// The blocking teardown is handed to a detached worker: this is invoked from
  /// the Android platform thread, which must never wait on a device operation.
  FFI_PLUGIN_EXPORT bool requestEngineTeardownForEngine(int64_t engine_id)
  {
    if (engine_id == kNoEngineId)
      return false;

    // Retire this engine's callables first, whatever the lifecycle decision
    // below turns out to be, and gated on callback ownership rather than the
    // lifecycle claim. The isolate that created them is going away and invoking
    // one afterwards is undefined behaviour, so this must not be conditional on
    // also being allowed to dispose the engine.
    //
    // An engine can legitimately own the callables without owning the lifecycle
    // claim — its initialization worker can win init_deinit_mutex after a later
    // engine has already claimed — and gating the clear on the claim would
    // leave that engine's callables live after its isolate died.
    clearDartCallbackRegistrationsForEngine(engine_id);

    // A different engine has claimed the native engine, so it is live and owns
    // its own teardown. An unclaimed engine means Dart already deinited
    // cleanly, leaving nothing to dispose.
    EngineLifecycleClaim claim;
    if (!tryBeginEngineTeardown(engine_id, &claim))
      return false;

    try
    {
      std::thread([claim]()
                  {
#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
        // Published when the worker is completely finished -- after
        // disposeLocked() has reset the Player and the backend is down.
        // `engine_initialized` goes false at the *start* of teardown, so it is
        // not the same state and a test that waits on it is not synchronized
        // with this worker at all.
        struct CompletionSignal
        {
          ~CompletionSignal()
          {
            engine_teardown_completed.fetch_add(1, std::memory_order_acq_rel);
          }
        } completionSignal;
#endif

        std::lock_guard<std::mutex> guard(init_deinit_mutex);
        std::lock_guard<std::mutex> guard_load(loadMutex);

        // A replacement engine claimed the native engine while this worker was
        // waiting for the mutex. Its engine is live and must not be torn down.
        if (!engineLifecycleClaimIsCurrent(claim))
          return;

        disposeLocked(&claim); })
          .detach();
    }
    catch (...)
    {
      // Thread creation failed. The bridges are already inert, and the next
      // init() still recovers by deiniting the stale engine itself.
      return false;
    }

    return true;
  }

#if defined(SOLOUD_LIFECYCLE_TEST_HOOKS)
  /// Test-only entry points, compiled out of every shipping build. See
  /// `test/engine_lifecycle_test.cpp`.
  ///
  /// The interleavings that matter on this path are between a lifecycle hook
  /// running on the platform thread and native work that already holds
  /// init_deinit_mutex. That mutex is internal and no exported call holds it
  /// for a controllable length of time, so without a hook a test can only hope
  /// the scheduler puts the teardown inside that window.

  /// Peak number of deferred idle-timeout workers alive at once. The
  /// coalescing is only meaningful if this stays at 1 no matter how many times
  /// the setter is called while init_deinit_mutex is held.
  FFI_PLUGIN_EXPORT int soloudTestIdleTimeoutWorkerPeak()
  {
    return idle_timeout_worker_peak.load(std::memory_order_acquire);
  }

  FFI_PLUGIN_EXPORT void soloudTestResetIdleTimeoutWorkerPeak()
  {
    idle_timeout_worker_peak.store(0, std::memory_order_release);
  }

  /// The policy the current Player is actually running with, as opposed to the
  /// process-global publication.
  FFI_PLUGIN_EXPORT int64_t soloudTestAppliedIdleTimeoutMs()
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    if (player.get() == nullptr)
      return 0;
    return player.get()->currentAudioDeviceIdleTimeoutMs();
  }

  /// Set or clear the *engine-level* state callback -- the pointer
  /// SoLoud::notifyStateChanged() dispatches through, which the miniaudio
  /// notification threads read. Deliberately without init_deinit_mutex,
  /// because that is exactly how teardown clears it relative to a notification
  /// already in flight.
  FFI_PLUGIN_EXPORT void soloudTestSetEngineStateCallback(unsigned int enable)
  {
    if (player.get() == nullptr)
      return;
    player.get()->setStateChangedCallback(enable != 0 ? stateChangedCallback
                                                      : nullptr);
  }

  /// How many FlutterEngine-owned teardown workers have run to completion.
  /// Distinct from isInited(): that goes false when teardown *starts*.
  FFI_PLUGIN_EXPORT int soloudTestEngineTeardownCompletedCount()
  {
    return engine_teardown_completed.load(std::memory_order_acquire);
  }

  FFI_PLUGIN_EXPORT void soloudTestLockInitDeinit()
  {
    init_deinit_mutex.lock();
  }

  FFI_PLUGIN_EXPORT void soloudTestUnlockInitDeinit()
  {
    init_deinit_mutex.unlock();
  }

  /// Dispatch through the native state-changed bridge, which is what a real
  /// engine event does. Used to prove a retired callable is never invoked.
  FFI_PLUGIN_EXPORT void soloudTestInvokeStateChanged(unsigned int state)
  {
    stateChangedCallback(state);
  }

  /// Dispatch through a real BufferStream/PullBufferStream, the way the audio
  /// thread does. These are the callables a retirement cannot reach directly,
  /// so this is what proves the generation gate covers them.
  ///
  /// Returns false when [hash] names no buffer-backed sound.
  FFI_PLUGIN_EXPORT bool soloudTestInvokeStreamCallbacks(unsigned int hash)
  {
    std::lock_guard<std::mutex> guard_load(loadMutex);
    if (player.get() == nullptr)
      return false;

    ActiveSound *sound = player.get()->findByHash(hash);
    if (sound == nullptr || sound->sound == nullptr)
      return false;

    AudioMetadata metadata;
    if (sound->soundType == SoundType::TYPE_PULL_BUFFER_STREAM)
    {
      auto *stream =
          static_cast<SoLoud::PullBufferStream *>(sound->sound.get());
      stream->callOnMoreDataIsNeededCallback(0);
      stream->callOnBufferingCallback(true, 0, 0.0);
      stream->callOnMetadataCallback(metadata);
      stream->callOnAudioDurationCallback(1.0);
      return true;
    }

    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
    {
      auto *stream = static_cast<SoLoud::BufferStream *>(sound->sound.get());
      stream->callOnBufferingCallback(true, 0, 0.0);
      stream->callOnMetadataCallback(metadata);
      return true;
    }

    return false;
  }

  /// Park an initialization inside initEngine(), just after the audio device
  /// has been opened. That is the window a real Android device-open occupies
  /// for seconds, and the one a FlutterEngine can be destroyed in.
  std::mutex test_init_barrier_mutex;
  std::condition_variable test_init_barrier_cv;
  bool test_init_barrier_armed = false;
  bool test_init_barrier_reached = false;

  static void soloudTestInitBarrier()
  {
    std::unique_lock<std::mutex> lock(test_init_barrier_mutex);
    if (!test_init_barrier_armed)
      return;

    test_init_barrier_reached = true;
    test_init_barrier_cv.notify_all();
    test_init_barrier_cv.wait(lock, [] { return !test_init_barrier_armed; });
  }

  FFI_PLUGIN_EXPORT void soloudTestArmInitBarrier()
  {
    std::lock_guard<std::mutex> guard(test_init_barrier_mutex);
    test_init_barrier_armed = true;
    test_init_barrier_reached = false;
  }

  FFI_PLUGIN_EXPORT void soloudTestWaitInitBarrierReached()
  {
    std::unique_lock<std::mutex> lock(test_init_barrier_mutex);
    test_init_barrier_cv.wait(lock, [] { return test_init_barrier_reached; });
  }

  FFI_PLUGIN_EXPORT void soloudTestReleaseInitBarrier()
  {
    {
      std::lock_guard<std::mutex> guard(test_init_barrier_mutex);
      test_init_barrier_armed = false;
    }
    test_init_barrier_cv.notify_all();
  }

  /// Drive the exact function MixerOutput holds on its notification thread.
  FFI_PLUGIN_EXPORT void soloudTestInvokeMixerOutput()
  {
    unsigned char scratch[4] = {0, 0, 0, 0};
    dispatchMixerOutputToDart(scratch, sizeof(scratch));
  }

  /// True while a Dart callable registered at the current generation may run.
  /// Lets a test observe the gate itself, not only its effect on one callable.
  FFI_PLUGIN_EXPORT int soloudTestCallbacksAreLive()
  {
    const uint64_t generation = dart_callbacks::currentGeneration();
    const dart_callbacks::InvocationPass pass;
    return pass.isLive(generation) ? 1 : 0;
  }

  /// Whether the Player itself is initialized, as opposed to the
  /// `engine_initialized` flag that prepareEngineInit() also lowers. This is
  /// what answers "did that teardown actually dispose the native engine?".
  FFI_PLUGIN_EXPORT int soloudTestPlayerIsInited()
  {
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    return (player.get() != nullptr && player.get()->isInited()) ? 1 : 0;
  }
#endif

#if defined(__ANDROID__)
  /// JNI entry points for FlutterSoloudPlugin. The names must match
  /// `flutter.soloud.flutter_soloud.FlutterSoloudPlugin` exactly (an underscore
  /// in a package or class name is escaped as `_1`); verify them with
  /// `javac -h` rather than by eye.
  JNIEXPORT jboolean JNICALL
  Java_flutter_soloud_flutter_1soloud_FlutterSoloudPlugin_nativeClearDartCallbackRegistrationsForEngine(
      JNIEnv *, jclass, jlong engine_id)
  {
    return clearDartCallbackRegistrationsForEngine(
               static_cast<int64_t>(engine_id))
               ? JNI_TRUE
               : JNI_FALSE;
  }

  JNIEXPORT jboolean JNICALL
  Java_flutter_soloud_flutter_1soloud_FlutterSoloudPlugin_nativeRequestEngineTeardownForEngine(
      JNIEnv *, jclass, jlong engine_id)
  {
    return requestEngineTeardownForEngine(static_cast<int64_t>(engine_id))
               ? JNI_TRUE
               : JNI_FALSE;
  }
#endif

  FFI_PLUGIN_EXPORT int isInited()
  {
    return engine_initialized.load(std::memory_order_acquire) ? 1 : 0;
  }

  /// Load a new sound to be played once or multiple times later.
  ///
  /// After loading the file, the [fileLoadedCallback] will call the
  /// Dart function defined with [setDartEventCallback] which gives back
  /// the error and the new hash.
  ///
  /// [completeFileName] the complete file path.
  /// [loadIntoMem] if true Soloud::wav will be used which loads
  /// all audio data into memory. This will be useful when
  /// the audio is short, ie for game sounds, mainly used to prevent
  /// gaps or lags when starting a sound (less CPU, more memory allocated).
  /// If false, Soloud::wavStream will be used and the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [loadIntoMem] = false
  /// [hash] return the hash of the sound
  /// Returns [PlayerErrors.noError] if success
  FFI_PLUGIN_EXPORT void loadFile(
      char *completeFileName,
      bool loadIntoMem,
      uint64_t counter)
  {
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);

    Player *p = player.get();
    if (p == nullptr || !p->isInited())
    {
      printf("WARNING (from SoLoud C++ binding code): the player has "
             "not yet been initialized. This is likely a bug in flutter_soloud. "
             "Please report the bug.\n");
      return;
    }

    unsigned int hash = 0;
    // std::thread loadThread([p, completeFileName, loadIntoMem, hash]()
    //                        {
    PlayerErrors error = p->loadFile(completeFileName, loadIntoMem, (unsigned int *)&hash);
    // soloud_platform_log("LOAD FILE FROM THREAD error: %d  hash: %u\n", error, hash);
    fileLoadedCallback(error, completeFileName, (unsigned int *)&hash, counter);
    // });
    // // TODO(marco): use .detach()? Use std::atomic somewhere
    // loadThread.join();
  }

  /// Load a new sound stored into [buffer] to be played once or multiple times
  /// later. Mainly used on web because the browsers are not allowed to read files
  /// directly.
  ///
  /// [uniqueName] the unique name of the sound. Used only to have the [hash].
  /// [buffer] the audio data. These contains the audio file bytes.
  /// [length] the length of [buffer].
  /// [loadIntoMem] if true Soloud::wav will be used which loads
  /// all audio data into memory. This will be useful when
  /// the audio is short, ie for game sounds, mainly used to prevent
  /// gaps or lags when starting a sound (less CPU, more memory allocated).
  /// If false, Soloud::wavStream will be used and the audio data is loaded
  /// from the given file when needed (more CPU, less memory allocated).
  /// See the [seek] note problem when using [loadIntoMem] = false
  /// [hash] return the hash of the sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors loadMem(char *uniqueName,
                                              unsigned char *buffer, int length,
                                              int loadIntoMem,
                                              unsigned int *hash)
  {
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);
    // this check is already been done in Dart
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return (PlayerErrors)player.get()->loadMem(uniqueName, buffer, length,
                                               loadIntoMem, *hash);
  }

  /// Set up an audio stream.
  ///
  /// [maxBufferSize] the max buffer size in **bytes**. When adding audio data
  /// using [addAudioDataStream] and this values is reached, the stream will
  /// be considered ended (likewise we called [setDataIsEnded]). This means that
  /// when playing it, it will stop at that point (if loop is not set).
  ///
  /// **Note:** this parameter doesn't allocate any memory, but it just limits
  /// the amount of data that can be added.
  ///
  /// [bufferingTimeNeeds] the buffering time needed in seconds. If a handle
  /// reaches the current buffer length, it will start to buffer pausing it and
  /// waiting until the buffer will have enough data to cover this time.
  ///
  /// [sampleRate] the sample rate. Usually is 22050 or 44100 (CD quality).
  /// When using [format] as `opus`, the sample rate can be 48000, 24000,
  /// 16000, 12000 or 8000. Whatever the sample rate of the incoming data is,
  /// it will be resampled to this value. So, if you are adding Opus data at
  /// 48 KHz, and you set this to 24000, the data will be resampled to 24 KHz.
  ///
  /// [channels] choose the number of channels. The `opus` format
  /// supports only mono and stereo.
  ///
  /// [format] choose from `f32le`, `s8`, `s16le`, `s32le` and
  /// `opus`. The last one is a special format that uses the Opus codec with
  /// Ogg container. It supports only 48, 24, 16, 12 and 8 KHz sample rates
  /// and mono and stereo.
  ///
  /// [onBufferingCallback] a callback that is called when starting to buffer
  /// (isBuffering = true) and when the buffering is done (isBuffering = false).
  /// The callback is called with the `handle` which triggered the event and
  /// the `time` in seconds.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  setBufferStream(unsigned int *hash, unsigned long maxBufferSize,
                  int bufferingType, double bufferingTimeNeeds,
                  unsigned int sampleRate, unsigned int channels, int format,
                  dartOnBufferingCallback_t onBufferingCallback,
                  dartOnMetadataCallback_t onMetadataCallback)
  {
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;

    // BufferType::OPUS is deprecated in favor of BufferType::AUTO wich
    // autodetects MP3, OGG Opus and OGG Vorbis formats
    if (format == BufferType::OPUS)
      format = BufferType::AUTO;

    unsigned int bytesPerSample = 4; // Default to 4 bytes for PCM_F32LE
    switch (format)
    {
    case BufferType::AUTO:
    case BufferType::PCM_F32LE:
      bytesPerSample = 4;
      break;
    case BufferType::PCM_S8:
      bytesPerSample = 1;
      break;
    case BufferType::PCM_S16LE:
      bytesPerSample = 2;
      break;
    case BufferType::PCM_S32LE:
      bytesPerSample = 4;
      break;
    default:
      bytesPerSample = 4;
      break;
    }
    PCMformat dataType = {sampleRate, channels, bytesPerSample,
                          (BufferType)format};
    PlayerErrors e = (PlayerErrors)player.get()->setBufferStream(
        *hash, maxBufferSize, (BufferingType)bufferingType, bufferingTimeNeeds,
        dataType, onBufferingCallback, onMetadataCallback);
    return e;
  }

  // Resets the buffer of the data stream.
  // [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors resetBufferStream(unsigned int hash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->resetBufferStream(hash);
  }

  /// Get the time consumed by the stream of a type `BufferingType.RELEASED`with
  /// hash [hash].
  FFI_PLUGIN_EXPORT enum PlayerErrors getStreamTimeConsumed(unsigned int hash,
                                                            float *timeConsumed)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;

    return player.get()->getStreamTimeConsumed(hash, timeConsumed);
  }

  /// Set the icy metadata integer value. Must be set once before calling
  /// the first time [addAudioDataStream] to be able to get MP3 metadata
  /// of a stream.
  ///
  /// **Note:** this function is only for MP3 streams. It must
  /// be called before calling [addAudioDataStream] to be able to get MP3
  /// metadata of a stream. It will set the `icy-metaint` value of the
  /// MP3 stream to retrieve the metadata from the stream.
  /// When adding data, for example from an online stream, the request
  /// must contain the `icy-metaint` header:
  /// ```dart
  ///   http.StreamedResponse? currentStream;
  ///   client = http.Client();
  ///   final request = http.Request('GET', Uri.parse(url));
  ///   request.headers.addAll({'Icy-MetaData': '1'});
  /// ```
  /// When the first chunk of data has been received, the `icy-metaint`
  /// value can be read as follows:
  /// ```dart
  /// bool mp3IcyMetaIntSent = false;
  /// currentStream!.stream.listen(
  ///   (data) {
  ///     if (!mp3IcyMetaIntSent) {
  ///         mp3IcyMetaIntSent = true;
  ///         // set it when receiving the first audio chunk
  ///         SoLoud.instance.setMp3BufferIcyMetaInt(
  ///             sound,
  ///             int.parse(currentStream!.headers['icy-metaint'] ?? '0'),
  ///         );
  ///     }
  ///     ...
  ///   ```
  ///
  /// [hash] the hash of the stream sound.
  /// [icyMetaInt] the icy metadata integer value. Default is 16000 which
  /// is the most used value.
  FFI_PLUGIN_EXPORT enum PlayerErrors setBufferIcyMetaInt(unsigned int hash,
                                                          int icyMetaInt)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (icyMetaInt < 0)
      icyMetaInt = 0;
    return player.get()->setBufferIcyMetaInt(hash, icyMetaInt);
  }

  /// [audioSizeBytes] the total size in **bytes** of the original encoded or
  /// PCM stream. This is used to compute the total audio duration and to
  /// request the tail chunk for formats where the duration is not in the
  /// header (Ogg Vorbis/Opus).
  ///
  /// [onBufferingCallback] a callback that is called when starting to buffer
  /// and when the buffering is done.
  ///
  /// [onMetadataCallback] a callback that is called when metadata is detected.
  ///
  /// [onMoreDataIsNeededCallback] a callback that is called when the engine
  /// needs more encoded audio data. The parameter is the byte offset in the
  /// original encoded stream.
  ///
  /// [onAudioDurationCallback] a callback that is called once the total audio
  /// duration has been determined.
  FFI_PLUGIN_EXPORT enum PlayerErrors setPullBufferStream(
      unsigned int *hash, unsigned int bufferSizeBytes,
      double bufferTriggerPosition, unsigned int sampleRate,
      unsigned int channels, int format, uint64_t audioSizeBytes,
      dartOnBufferingCallback_t onBufferingCallback,
      dartOnMetadataCallback_t onMetadataCallback,
      dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
      dartOnAudioDurationCallback_t onAudioDurationCallback)
  {
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;

    if (format == BufferType::OPUS)
      format = BufferType::AUTO;

    PlayerErrors e = (PlayerErrors)player.get()->setPullBufferStream(
        *hash, bufferSizeBytes, bufferTriggerPosition, sampleRate, channels,
        (BufferType)format, audioSizeBytes, onBufferingCallback,
        onMetadataCallback, onMoreDataIsNeededCallback, onAudioDurationCallback);
    return e;
  }

  FFI_PLUGIN_EXPORT enum PlayerErrors resetPullBufferStream(unsigned int hash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->resetPullBufferStream(hash);
  }

  FFI_PLUGIN_EXPORT enum PlayerErrors
  addPullBufferDataStream(unsigned int hash, const unsigned char *data,
                          unsigned int aDataLen, uint64_t offset)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->addPullBufferDataStream(hash, data, aDataLen, offset);
  }

  FFI_PLUGIN_EXPORT enum PlayerErrors
  getPullBufferTimeRange(unsigned int hash, double *startTime, double *endTime)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->getPullBufferTimeRange(hash, startTime, endTime);
  }

  FFI_PLUGIN_EXPORT enum PlayerErrors
  addAudioDataStream(unsigned int hash, const unsigned char *data,
                     unsigned int aDataLen)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->addAudioDataStream(hash, data, aDataLen);
  }

  // Set the end of the data stream.
  // [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors setDataIsEnded(unsigned int hash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->setDataIsEnded(hash);
  }

  // Get the current buffer size in bytes of this sound with hash [hash].
  // [hash] the hash of the stream sound.
  FFI_PLUGIN_EXPORT enum PlayerErrors getBufferSize(unsigned int hash,
                                                    unsigned int *sizeInBytes)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->getBufferSize(hash, sizeInBytes);
  }

  /// Load a new waveform to be played once or multiple times later
  ///
  /// [waveform]  WAVE_SQUARE = 0,
  ///             WAVE_SAW,
  ///             WAVE_SIN,
  ///             WAVE_TRIANGLE,
  ///             WAVE_BOUNCE,
  ///             WAVE_JAWS,
  ///             WAVE_HUMPS,
  ///             WAVE_FSQUARE,
  ///             WAVE_FSAW
  /// [superWave]
  /// [scale]
  /// [detune]
  /// [hash] return hash of the sound
  /// Returns [PlayerErrors.noError] if success
  FFI_PLUGIN_EXPORT enum PlayerErrors loadWaveform(int waveform, bool superWave,
                                                   float scale, float detune,
                                                   unsigned int *hash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return (PlayerErrors)player.get()->loadWaveform(waveform, superWave, scale,
                                                    detune, *hash);
  }

  /// Set the scale of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newScale]
  FFI_PLUGIN_EXPORT void setWaveformScale(unsigned int hash, float newScale)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;

    player.get()->setWaveformScale(hash, newScale);
  }

  /// Set the detune of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newDetune]
  FFI_PLUGIN_EXPORT void setWaveformDetune(unsigned int hash, float newDetune)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;

    player.get()->setWaveformDetune(hash, newDetune);
  }

  /// Set a new frequency of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newFreq]

  /// Thread to oscillate the frequency
  std::thread waveformFreqThread;
  FFI_PLUGIN_EXPORT void setWaveformFreq(unsigned int hash, float newFreq)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;

    player.get()->setWaveformFreq(hash, newFreq);
  }

  /// Set a new frequence of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [superwave]
  FFI_PLUGIN_EXPORT void setSuperWave(unsigned int hash, bool superwave)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;

    player.get()->setWaveformSuperwave(hash, superwave);
  }

  /// Set a new wave form of an already loaded waveform identified by [hash]
  ///
  /// [hash] the unique sound hash of a waveform sound
  /// [newWaveform]   WAVE_SQUARE = 0,
  ///                 WAVE_SAW,
  ///                 WAVE_SIN,
  ///                 WAVE_TRIANGLE,
  ///                 WAVE_BOUNCE,
  ///                 WAVE_JAWS,
  ///                 WAVE_HUMPS,
  ///                 WAVE_FSQUARE,
  ///                 WAVE_FSAW
  FFI_PLUGIN_EXPORT void setWaveform(unsigned int hash, int newWaveform)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;

    player.get()->setWaveform(hash, newWaveform);
  }

  /// Speech the text given
  ///
  /// [textToSpeech]
  /// Returns [PlayerErrors.noError] if success and [handle] sound identifier
  /// TODO(marco): add other T2S parameters
  FFI_PLUGIN_EXPORT enum PlayerErrors speechText(char *textToSpeech,
                                                 unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return (PlayerErrors)player.get()->textToSpeech(textToSpeech, *handle);
  }

  /// Switch pause state for an already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid. Unpausing posts an asynchronous device start, so
  /// this never reports [PlayerErrors.audioDeviceFailedToStart].
  FFI_PLUGIN_EXPORT enum PlayerErrors pauseSwitch(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->pauseSwitch(handle);
  }

  /// Pause or unpause already loaded sound identified by [handle]
  ///
  /// [handle] the sound handle
  /// [pause] the sound handle
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid. Unpausing posts an asynchronous device start, so
  /// this never reports [PlayerErrors.audioDeviceFailedToStart].
  FFI_PLUGIN_EXPORT enum PlayerErrors setPause(unsigned int handle, bool pause)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return player.get()->setPause(handle, pause);
  }

  /// Gets the pause state
  ///
  /// [handle] the sound handle
  /// Return true if paused
  FFI_PLUGIN_EXPORT int getPause(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return false;
    return player.get()->getPause(handle) ? 1 : 0;
  }

  /// Set a sound's relative play speed.
  /// Setting the value to 0 will cause undefined behavior, likely a crash.
  /// Change the relative play speed of a sample. This changes the effective
  /// sample rate while leaving the base sample rate alone.
  ///
  /// Note that playing a sound at a higher sample rate will require SoLoud
  /// to request more samples from the sound source, which will require more
  /// memory and more processing power. Playing at a slower sample rate
  /// is cheaper.
  ///
  /// [handle] the sound handle
  /// [speed] the new speed
  FFI_PLUGIN_EXPORT void setRelativePlaySpeed(unsigned int handle, float speed)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setRelativePlaySpeed(handle, speed);
  }

  /// Get a sound's relative play speed.
  /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.
  ///
  /// [handle] the sound handle
  /// Return the current play speed.
  FFI_PLUGIN_EXPORT float getRelativePlaySpeed(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 1;
    return player.get()->getRelativePlaySpeed(handle);
  }

  /// Gets the approximate volume for output per output channel (i.e, per speaker).
  ///
  /// [channel] the channel.
  /// Return zero for invalid parameters.
  FFI_PLUGIN_EXPORT float getApproximateVolume(unsigned int channel)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->getApproximateVolume(channel);
  }

  /// Play already loaded sound identified by [hash]
  ///
  /// Play an already loaded sound.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [paused] 0 not paused
  /// [looping] whether to start the sound in looping state.
  /// [loopingStartAt] If looping is enabled, the loop point is, by default,
  /// the start of the stream. The loop start point can be set with this
  /// parameter.
  /// [loopingEndAt] If greater than zero, loop before this time. Zero uses the
  /// natural end of the stream.
  /// [scale] relative playback speed multiplier (1.0f = normal speed).
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors play(
      unsigned int soundHash, unsigned int busId, float volume, float pan,
      bool paused, bool looping, double loopingStartAt, double loopingEndAt,
      float scale, unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    PlayerErrors result = player.get()->play(soundHash, *handle, busId, volume, pan,
                                             paused, looping, loopingStartAt,
                                             loopingEndAt, scale);
    return result;
  }

  /// Variant of [play] that takes an additional parameter, the time offset
  /// for the sound.
  ///
  /// While the vanilla [play] tries to play sounds as soon as possible,
  /// [playClocked] will delay the start of sounds so that rapidly launched
  /// sounds don't all get clumped to the start of the next outgoing sound
  /// buffer.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [soundTime] your app's "physics time", in seconds. SoLoud will use that
  /// time (as well as the time previously used) to calculate the delay
  /// between two sound effects.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors playClocked(
      unsigned int soundHash, double soundTime, unsigned int busId,
      float volume, float pan, float scale, bool looping,
      double loopingStartAt, double loopingEndAt, unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    PlayerErrors result = player.get()->playClocked(
        soundHash, *handle, soundTime, busId, volume, pan, scale,
        looping, loopingStartAt, loopingEndAt);
    return result;
  }

  /// Set the number of samples to delay before starting to play a sound.
  ///
  /// This is used internally by [playClocked]. In the unlikely event that
  /// you may want to use it manually, it's available here. Note that calling
  /// this on a "live" voice will cause silence to be inserted at the start
  /// of the next audio buffer.
  ///
  /// [handle] the sound handle
  /// [samples] the number of samples to delay the sound with
  FFI_PLUGIN_EXPORT void setDelaySamples(unsigned int handle,
                                         unsigned int samples)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->setDelaySamples(handle, samples);
  }

  /// Get the current stream time of a voice, in seconds.
  ///
  /// [handle] the sound handle
  /// Return the stream time in seconds. 0 if [handle] is invalid.
  FFI_PLUGIN_EXPORT double getStreamTime(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    return player.get()->getStreamTime(handle);
  }

  /// Reset the clock used by [playClocked] and [play3dClocked] to the state
  /// as if they were never called.
  ///
  /// The next clocked play will anchor the caller's "physics time" to the
  /// audio clock again (leading by two output buffers).
  FFI_PLUGIN_EXPORT void resetStreamTime()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->resetStreamTime();
  }

  /// Get the engine's global stream time, in seconds.
  ///
  /// This is the clock the mixer advances at the start of every output
  /// buffer and the time base used by [playScheduled], [stopScheduled] and
  /// [fadeScheduled]. It only advances while the audio device is mixing.
  ///
  /// Return the engine time in seconds.
  FFI_PLUGIN_EXPORT double getEngineTime()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    return player.get()->getEngineTime();
  }

  /// Engine time of the sample currently reaching the output device: the mix
  /// clock (see [getEngineTime]) minus the render-ahead ring depth. Equals
  /// [getEngineTime] when the render-ahead ring is disabled (the default).
  FFI_PLUGIN_EXPORT double getPlayheadTime()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    return player.get()->getPlayheadTime();
  }

  /// Estimated output latency in seconds (render-ahead ring depth plus one
  /// device period). 0 when the render-ahead ring is disabled.
  FFI_PLUGIN_EXPORT double getOutputLatency()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    return player.get()->getOutputLatency();
  }

  /// Whether the render-ahead ring (the retroactive re-mix prerequisite) is
  /// active. Enabled at init time via `initEngine`'s `renderAheadFrames`.
  FFI_PLUGIN_EXPORT unsigned int isRenderAheadEnabled()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->isRenderAheadEnabled() ? 1 : 0;
  }

  /// Start playing a sound at an absolute engine time (see [getEngineTime]),
  /// with sample accuracy.
  ///
  /// Unlike [playClocked] there is no anchor and no re-anchor guard, so
  /// sounds can be scheduled arbitrarily far in the future. A time in the
  /// past plays as soon as possible.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [atTime] the absolute engine time, in seconds, at which the sound
  /// should start
  /// [duration] if greater than zero, the sound is automatically stopped
  /// at [atTime] + [duration]
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [volume] 1.0f full volume
  /// [pan] 0.0f centered
  /// [scale] relative playback speed multiplier (1.0f = normal speed)
  /// [looping] whether the sound loops upon reaching the end
  /// [loopingStartAt] time position in seconds to restart playback when looping
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT enum PlayerErrors playScheduled(
      unsigned int soundHash, double atTime, double duration,
      unsigned int busId, float volume, float pan, float scale,
      bool looping, double loopingStartAt, double loopingEndAt,
      unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    PlayerErrors result = player.get()->playScheduled(
        soundHash, *handle, atTime, duration, busId, volume, pan, scale,
        looping, loopingStartAt, loopingEndAt);
    return result;
  }

  /// Stop a sound at an absolute engine time (see [getEngineTime]).
  ///
  /// A time in the past stops the sound immediately.
  ///
  /// [handle] the sound handle
  /// [atTime] the absolute engine time, in seconds, at which the sound
  /// should stop
  FFI_PLUGIN_EXPORT void stopScheduled(unsigned int handle, double atTime)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->stopScheduled(handle, atTime);
  }

  /// Fade the volume of a sound starting at an absolute engine time
  /// (see [getEngineTime]).
  ///
  /// The fade goes from the volume the sound has at call time to [to] over
  /// [fadeTime] seconds. If [thenStop] is true, the sound is stopped when
  /// the fade ends (at [atTime] + [fadeTime]).
  ///
  /// [handle] the sound handle
  /// [atTime] the absolute engine time, in seconds, at which the fade
  /// should start. A time in the past starts the fade immediately.
  /// [to] the ending volume of the fade
  /// [fadeTime] the duration of the fade, in seconds
  /// [thenStop] whether to stop the sound when the fade ends
  FFI_PLUGIN_EXPORT void fadeScheduled(unsigned int handle, double atTime,
                                       float to, double fadeTime,
                                       bool thenStop)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->fadeScheduled(handle, atTime, to, fadeTime, thenStop);
  }

  /// Stop already loaded sound identified by [handle] and clear it
  ///
  /// [handle]
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.soundHandleNotFound] if
  /// [handle] is not valid (for example the voice has already ended).
  FFI_PLUGIN_EXPORT enum PlayerErrors stop(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    const enum PlayerErrors error = player.get()->stop(handle);
    if (error != noError)
      return error;
    return noError;
  }

  /// Stop all playing voices without disposing the loaded sounds.
  ///
  /// Each stopped voice triggers the voice-ended callback (dispatched by
  /// SoLoud itself), so Dart is notified for every handle like with [stop].
  FFI_PLUGIN_EXPORT void stopAll()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->stopAll();
  }

  /// Stop all voices playing the already loaded sound identified by
  /// [soundHash] without disposing it.
  ///
  /// Each stopped voice triggers the voice-ended callback (dispatched by
  /// SoLoud itself), so Dart is notified for every handle like with [stop].
  FFI_PLUGIN_EXPORT void stopAudioSource(unsigned int soundHash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->stopAudioSource(soundHash);
  }

  /// Stop all handles of the already loaded sound identified by [hash] and
  /// dispose it
  ///
  /// [soundHash]
  FFI_PLUGIN_EXPORT void disposeSound(unsigned int soundHash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);
    try
    {
      player.get()->disposeSound(soundHash);
    }
    catch (const std::exception &e)
    {
      printf("Error in disposeSound: %s\n", e.what());
    }
    catch (...)
    {
      printf("Unknown error in disposeSound\n");
    }
  }

  /// Dispose all sounds already loaded
  ///
  FFI_PLUGIN_EXPORT void disposeAllSound()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    std::lock_guard<std::mutex> guard_init(init_deinit_mutex);
    std::lock_guard<std::mutex> guard_load(loadMutex);
    player.get()->disposeAllSound();
  }

  /// Query whether a sound is set to loop.
  ///
  /// [handle]
  /// Returns true if flagged for looping.
  FFI_PLUGIN_EXPORT int getLooping(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 0;
    return player.get()->getLooping(handle) == 1;
  }

  /// This function can be used to set a sample to play on repeat,
  /// instead of just playing once
  ///
  /// [soundHash]
  /// [enable]
  FFI_PLUGIN_EXPORT void setLooping(unsigned int handle, bool enable)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setLooping(handle, enable);
  }

  /// Get sound loop point value.
  ///
  /// [handle]
  /// Returns the time in seconds.
  FFI_PLUGIN_EXPORT double getLoopPoint(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 0;
    return player.get()->getLoopPoint(handle);
  }

  /// Set sound loop point value.
  ///
  /// [handle]
  /// [time] in seconds.
  FFI_PLUGIN_EXPORT void setLoopPoint(unsigned int handle, double time)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setLoopPoint(handle, time);
  }

  /// Get the sound loop end point value.
  ///
  /// [handle]
  /// Returns the time in seconds, or zero for the natural stream end.
  FFI_PLUGIN_EXPORT double getLoopEndPoint(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 0;
    return player.get()->getLoopEndPoint(handle);
  }

  /// Set the sound loop end point value.
  ///
  /// [handle]
  /// [time] in seconds, or zero to use the natural stream end.
  FFI_PLUGIN_EXPORT void setLoopEndPoint(unsigned int handle, double time)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setLoopEndPoint(handle, time);
  }

  /// Enable or disable visualization
  ///
  /// [enabled] enable or disable it
  FFI_PLUGIN_EXPORT void setVisualizationEnabled(bool enabled)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->setVisualizationEnabled(enabled);
  }

  /// Get visualization state
  ///
  /// Return true if enabled
  FFI_PLUGIN_EXPORT int getVisualizationEnabled()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->isVisualizationEnabled();
  }

  /// Returns valid data only if VisualizationEnabled is true
  ///
  /// Return a 256 float array containing FFT data.
  FFI_PLUGIN_EXPORT void getFft(float **fft, bool *isTheSameAsBefore)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isVisualizationEnabled())
      return;
    *fft = player.get()->calcFFT(isTheSameAsBefore);
  }

  /// Returns valid data only if VisualizationEnabled is true
  ///
  /// Return a 256 float array containing wave data.
  FFI_PLUGIN_EXPORT void getWave(float **wave, bool *isTheSameAsBefore)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isVisualizationEnabled())
      return;
    *wave = player.get()->getWave(isTheSameAsBefore);
  }

  /// Smooth FFT data.
  /// When new data is read and the values are decreasing, the new value will be
  /// decreased with an amplitude between the old and the new value.
  /// This will result on a less shaky visualization.
  ///
  /// [smooth] must be in the [0.0 ~ 1.0] range.
  /// 0 = no smooth
  /// 1 = full smooth
  /// the new value is calculated with:
  /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
  FFI_PLUGIN_EXPORT void setFftSmoothing(float smooth)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    analyzer.get()->setSmoothing(smooth);
  }

  /// Return in [samples] a 512 float array.
  /// The first 256 floats represent the FFT frequencies data [>=0.0].
  /// The other 256 floats represent the wave data (amplitude) [-1.0~1.0].
  ///
  /// [samples] should be allocated and freed in dart side
  float texture[512];
  FFI_PLUGIN_EXPORT void getAudioTexture(float **samples,
                                         bool *isTheSameAsBefore)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        analyzer.get() == nullptr || !player.get()->isVisualizationEnabled())
    {
      *samples = texture;
      memset(*samples, 0, sizeof(float) * 512);
      *isTheSameAsBefore = true;
      return;
    }
    float *wave = player.get()->getWave(isTheSameAsBefore);
    float *fft = analyzer.get()->calcFFT(wave);

    if (*isTheSameAsBefore)
    {
      *samples = texture;
      return;
    }

    memcpy(texture, fft, sizeof(float) * 256);
    memcpy(texture + 256, wave, sizeof(float) * 256);
    *samples = texture;
    *isTheSameAsBefore = false;
  }

  /// Return a floats matrix of 256x512
  /// Every row are composed of 256 FFT values plus 256 of wave data
  /// Every time is called, a new row is stored in the
  /// first row and all the previous rows are shifted
  /// up (the last one will be lost).
  ///
  /// [samples]
  float texture2D[256][512];
  FFI_PLUGIN_EXPORT void getAudioTexture2D(float **samples,
                                           bool *isTheSameAsBefore)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        analyzer.get() == nullptr || !player.get()->isVisualizationEnabled())
    {
      *samples = *texture2D;
      memset(*samples, 0, sizeof(float) * 512 * 256);
      *isTheSameAsBefore = true;
      return;
    }

    float *wave = player.get()->getWave(isTheSameAsBefore);
    float *fft = analyzer.get()->calcFFT(wave);
    if (*isTheSameAsBefore)
    {
      *samples = *texture2D;
      return;
    }

    /// shift up 1 row
    memmove(texture2D[1], texture2D[0], sizeof(float) * 512 * 255);
    /// store the new 1st row
    memcpy(texture2D[0], fft, sizeof(float) * 256);
    memcpy(texture2D[0] + 256, wave, sizeof(float) * 256);

    *samples = *texture2D;
    *isTheSameAsBefore = false;
  }

  FFI_PLUGIN_EXPORT float getTextureValue(int row, int column)
  {
    return texture2D[row][column];
  }

  /// Get the sound length in seconds
  ///
  /// [soundHash] the sound hash
  /// Returns sound length in seconds
  FFI_PLUGIN_EXPORT double getLength(unsigned int soundHash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    auto f = player.get()->getLength(soundHash);
    return f;
  }

  /// Seek playing in [time] seconds
  /// [time]
  /// [handle] the sound handle
  /// Returns [PlayerErrors.noError] if success
  ///
  /// NOTE: when seeking an mp3 file loaded using `loadIntoMem`=false
  /// the seek operation is not performed due to lags. This occurs because the
  /// mp3 codec must compute each frame length to gain a new position.
  /// The problem is explained in souloud_wavstream.cpp
  /// in `WavStreamInstance::seek` function.
  ///
  /// This mode is useful ie for background music, not for a music player
  /// where a seek slider for mp3s is a must.
  /// If you need seeking mp3, please, use `loadIntoMem`=true instead
  /// or other audio formats!
  ///
  FFI_PLUGIN_EXPORT enum PlayerErrors seek(unsigned int handle, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    return (PlayerErrors)player.get()->seek(handle, time);
  }

  /// Get current sound position  in seconds
  ///
  /// [handle] the sound handle
  /// Returns time in seconds
  FFI_PLUGIN_EXPORT double getPosition(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 0.0;
    return player.get()->getPosition(handle);
  }

  /// Get current Global volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT double getGlobalVolume()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0;
    return player.get()->getGlobalVolume();
  }

  /// Set current Global volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT enum PlayerErrors setGlobalVolume(float volume)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->setGlobalVolume(volume);
    return noError;
  }

  /// Get current [handle] volume
  ///
  /// Returns the volume
  FFI_PLUGIN_EXPORT double getVolume(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return 0.0;
    return player.get()->getVolume(handle);
  }

  /// Set current [handle] volume
  ///
  FFI_PLUGIN_EXPORT enum PlayerErrors setVolume(unsigned int handle,
                                                float volume)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (!player.get()->isValidHandle(handle))
      return soundHandleNotFound;
    player.get()->setVolume(handle, volume);
    return noError;
  }

  /// Get a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// Returns the range of the pan values is -1 to 1, where -1 is left, 0 is
  /// middle and and 1 is right.
  FFI_PLUGIN_EXPORT double getPan(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0.0f;

    return player.get()->getPan(handle);
  }

  /// Set a sound's current pan setting.
  ///
  /// [handle] the sound handle.
  /// [pan] the range of the pan values is -1 to 1, where -1 is left, 0 is middle
  /// and and 1 is right.
  FFI_PLUGIN_EXPORT void setPan(unsigned int handle, double pan)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    // Rounding to 6 decimal to work around the float to double precision.
    player.get()->setPan(handle, pan);
  }

  /// Set the left/right volumes directly.
  /// Note that this does not affect the value returned by getPan.
  ///
  /// [handle] the sound handle.
  /// [panLeft] value for the left pan.
  /// [panRight] value for the right pan.
  FFI_PLUGIN_EXPORT void setPanAbsolute(unsigned int handle, double panLeft,
                                        double panRight)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->setPanAbsolute(handle, panLeft, panRight);
  }

  /// Check if a handle is still valid.
  ///
  /// [handle] handle to check
  /// Return true if it still exists
  FFI_PLUGIN_EXPORT int getIsValidVoiceHandle(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return false;
    return player.get()->isValidHandle(handle) ? 1 : 0;
  }

  /// Returns the number of concurrent sounds that are playing at the moment.
  FFI_PLUGIN_EXPORT unsigned int getActiveVoiceCount()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->getActiveVoiceCount_internal();
  }

  /// Returns the number of concurrent sounds that are playing a specific audio
  /// source.
  FFI_PLUGIN_EXPORT int countAudioSource(unsigned int soundHash)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->countAudioSource(soundHash);
  }

  /// Returns the number of voices the application has told SoLoud to play.
  FFI_PLUGIN_EXPORT unsigned int getVoiceCount()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->getVoiceCount();
  }

  /// Get a sound's protection state.
  FFI_PLUGIN_EXPORT bool getProtectVoice(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return false;
    return player.get()->getProtectVoice(handle);
  }

  /// Set a sound's protection state.
  ///
  /// Normally, if you try to play more sounds than there are voices,
  /// SoLoud will kill off the oldest playing sound to make room.
  /// This will most likely be your background music. This can be worked
  /// around by protecting the sound.
  /// If all voices are protected, the result will be undefined.
  ///
  /// [handle]  handle to check.
  /// [protect] whether to protect or not.
  FFI_PLUGIN_EXPORT void setProtectVoice(unsigned int handle, bool protect)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setProtectVoice(handle, protect);
  }

  /// Set the inaudible behavior of a live sound. By default,
  /// if a sound is inaudible, it's paused, and will resume when it
  /// becomes audible again. With this function you can tell SoLoud
  /// to either kill the sound if it becomes inaudible, or to keep
  /// ticking the sound even if it's inaudible.
  ///
  /// [handle]  handle to check.
  /// [mustTick] whether to keep ticking or not when the sound becomes inaudible.
  /// [kill] whether to kill the sound or not when the sound becomes inaudible.
  FFI_PLUGIN_EXPORT void setInaudibleBehavior(unsigned int handle, bool mustTick,
                                              bool kill)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        !player.get()->isValidHandle(handle))
      return;
    player.get()->setInaudibleBehavior(handle, mustTick, kill);
  }

  /// Get the current maximum active voice count.
  FFI_PLUGIN_EXPORT unsigned int getMaxActiveVoiceCount()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return 0;
    return player.get()->getMaxActiveVoiceCount();
  }

  /// Set the current maximum active voice count.
  /// If voice count is higher than the maximum active voice count,
  /// SoLoud will pick the ones with the highest volume to actually play.
  /// [maxVoiceCount] the max concurrent sounds that can be played.
  ///
  /// NOTE: The number of concurrent voices is limited, as having unlimited
  /// voices would cause performance issues, as well as lead to unnecessary
  /// clipping. The default number of concurrent voices is 16, but this can be
  /// adjusted at runtime. The hard maximum number is 4095, but if more are
  /// required, SoLoud can be modified to support more. But seriously, if you need
  /// more than 4095 sounds at once, you're probably going to make some serious
  /// changes in any case.
  FFI_PLUGIN_EXPORT void setMaxActiveVoiceCount(unsigned int maxVoiceCount)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->setMaxActiveVoiceCount(maxVoiceCount);
  }

  /////////////////////////////////////////
  /// voice groups
  /////////////////////////////////////////

  /// Used to create a new voice group. Returns 0 if not successful.
  FFI_PLUGIN_EXPORT unsigned int createVoiceGroup()
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return -1;
    auto ret = player.get()->createVoiceGroup();
    return ret;
  }

  /// Deallocates the voice group. Does not stop the voices attached to the
  /// voice group.
  ///
  /// [handle] the group handle to destroy.
  FFI_PLUGIN_EXPORT void destroyVoiceGroup(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->destroyVoiceGroup(handle);
  }

  /// Adds voice handle to the voice group. The voice handles can still be
  /// used separate from the group.
  /// [voiceGroupHandle] the group handle to add the new [voiceHandle].
  /// [voiceHandle] voice handle to add to the [voiceGroupHandle].
  FFI_PLUGIN_EXPORT void addVoiceToGroup(unsigned int voiceGroupHandle,
                                         unsigned int voiceHandle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return;
    player.get()->addVoiceToGroup(voiceGroupHandle, voiceHandle);
  }

  /// Checks if the handle is a valid voice group. Does not care if the
  /// voice group is empty.
  ///
  /// [handle] the group handle to check.
  /// Return true if [handle] is a group handle.
  FFI_PLUGIN_EXPORT int isVoiceGroup(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return false;
    return player.get()->isVoiceGroup(handle);
  }

  /// Checks whether a voice group is empty. SoLoud automatically trims
  /// the voice groups of voices that have ended, so the group may be
  /// empty even though you've added valid voice handles to it.
  ///
  /// [handle] group handle to check.
  /// Return true if the group handle doesn't have any voices.
  FFI_PLUGIN_EXPORT int isVoiceGroupEmpty(unsigned int handle)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return false;
    return player.get()->isVoiceGroupEmpty(handle);
  }

  /////////////////////////////////////////
  /// faders & oscillators
  /////////////////////////////////////////

  /// Smoothly change the global volume over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadeGlobalVolume(float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->fadeGlobalVolume(to, time);
    return noError;
  }

  /// Smoothly change a channel's volume over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadeVolume(unsigned int handle, float to,
                                                 float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->fadeVolume(handle, to, time);
    return noError;
  }

  /// Smoothly change a channel's pan setting over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors fadePan(unsigned int handle, float to,
                                              float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->fadePan(handle, to, time);
    return noError;
  }

  /// Smoothly change a channel's relative play speed over specified time.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  fadeRelativePlaySpeed(unsigned int handle, float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->fadeRelativePlaySpeed(handle, to, time);
    return noError;
  }

  /// After specified time, pause the channel.
  FFI_PLUGIN_EXPORT enum PlayerErrors schedulePause(unsigned int handle,
                                                    float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->schedulePause(handle, time);
    return noError;
  }

  /// After specified time, stop the channel.
  FFI_PLUGIN_EXPORT enum PlayerErrors scheduleStop(unsigned int handle,
                                                   float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->scheduleStop(handle, time);
    return noError;
  }

  /// Set fader to oscillate the volume at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateVolume(unsigned int handle, float from, float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->oscillateVolume(handle, from, to, time);
    return noError;
  }

  /// Set fader to oscillate the panning at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillatePan(unsigned int handle, float from, float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->oscillatePan(handle, from, to, time);
    return noError;
  }

  /// Set fader to oscillate the relative play speed at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateRelativePlaySpeed(unsigned int handle, float from, float to,
                             float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->oscillateRelativePlaySpeed(handle, from, to, time);
    return noError;
  }

  /// Set fader to oscillate the global volume at specified frequency.
  FFI_PLUGIN_EXPORT enum PlayerErrors oscillateGlobalVolume(float from, float to,
                                                            float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    player.get()->oscillateGlobalVolume(from, to, time);
    return noError;
  }

  /////////////////////////////////////////
  /// Filters
  /////////////////////////////////////////

  /// Check if the given filter is active or not.
  ///
  /// [soundHash] the sound to check the filter. If this is =0 this function searches in the global filters.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to check.
  /// Returns [PlayerErrors.noError] if no errors and the index of
  /// the given filter (-1 if the filter is not active).
  FFI_PLUGIN_EXPORT enum PlayerErrors
  isFilterActive(unsigned int soundHash, unsigned int busId, enum FilterType filterType, int *index)
  {
    *index = -1;
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;

    if (soundHash == 0 && busId == 0)
      *index = player.get()->mFilters.isFilterActive(filterType);
    else
    {
      if (soundHash != 0)
      {
        auto const s = player.get()->findByHash(soundHash);
        if (s == nullptr)
          return soundHashNotFound;
        *index = s->filters->isFilterActive(filterType);
      }
      else
      {
        auto *busData = player.get()->findBusData(busId);
        if (busData == nullptr)
          return busIdNotFound;
        *index = busData->filters.isFilterActive(filterType);
      }
    }

    return noError;
  }

  /// Get parameters names of the given filter.
  ///
  /// [filterType] filter to get param names.
  /// Returns [PlayerErrors.noError] if no errors and the list of param names.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  getFilterParamNames(enum FilterType filterType, int *paramsCount,
                      char **names)
  {
    *paramsCount = 0;
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    std::vector<std::string> pNames =
        player.get()->mFilters.getFilterParamNames(filterType);
    *paramsCount = static_cast<int>(pNames.size());
    *names = (char *)malloc(sizeof(char *) * *paramsCount);
    // printf("C  paramsCount: %p  **names: %p\n", paramsCount, names);
    for (int i = 0; i < *paramsCount; i++)
    {
      names[i] = strdup(pNames[i].c_str());
      printf("C  i: %d  names[i]: %s  names[i]: %p\n", i, names[i], names[i]);
    }
    return noError;
  }

  /// Add the filter [filterType] to [soundHash]. If [soundHash]==0 the
  /// filter is added to global filters.
  ///
  /// [soundHash] the sound to add the filter to.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to add.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors addFilter(unsigned int soundHash,
                                                unsigned int busId,
                                                enum FilterType filterType)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (soundHash == 0 && busId == 0)
      return player.get()->mFilters.addFilter(filterType);

    if (soundHash != 0)
    {
      auto const s = player.get()->findByHash(soundHash);
      if (s == nullptr)
        return soundHashNotFound;
      return s->filters->addFilter(filterType);
    }
    else
    {
      auto *busFilters = player.get()->findBusData(busId);
      if (busFilters == nullptr)
        return busIdNotFound;
      return busFilters->filters.addFilter(filterType);
    }
    return PlayerErrors::noError;
  }

  /// Remove the filter [filterType] from [soundHash]. If [soundHash]==0 the
  /// filter is removed from the global filters.
  ///
  /// [soundHash] the sound to add the filter to.
  /// [busId] the bus to check the filter.
  /// If both [soundHash] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to remove.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors removeFilter(unsigned int soundHash,
                                                   unsigned int busId,
                                                   enum FilterType filterType)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (soundHash == 0 && busId == 0)
    {
      if (!player.get()->mFilters.removeFilter(filterType))
        return filterNotFound;
    }
    else
    {
      if (soundHash != 0)
      {
        auto const s = player.get()->findByHash(soundHash);
        if (s == nullptr)
          return soundHashNotFound;
        if (!s->filters->removeFilter(filterType))
          return filterNotFound;
      }
      else
      {
        auto *busFilters = player.get()->findBusData(busId);
        if (busFilters == nullptr)
          return busIdNotFound;
        if (!busFilters->filters.removeFilter(filterType))
          return filterNotFound;
      }
    }

    return PlayerErrors::noError;
  }

  /// Set the effect parameter with id [attributeId]
  /// of [filterType] with [value] value.
  ///
  /// [handle] the handle to set the filter to. If equal to 0, the filter is applyed globally.
  /// [busId] the bus to set the filter to.
  /// If both [handle] and [busId] are =0 this function sets the global filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute id of the filter to modify.
  /// [value] the value to set the attribute to.
  ///
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors setFilterParams(unsigned int handle,
                                                      unsigned int busId,
                                                      enum FilterType filterType,
                                                      int attributeId,
                                                      float value)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    /// Not important to call the [SoLoud::AudioSource].setFilterParams() here,
    /// SoLoud will set the param globally or by [handle] if  [handle] is not ==0.
    if (handle == 0 && busId == 0)
    {
      player.get()->mFilters.setFilterParams(handle, filterType, attributeId, value);
    }
    else
    {
      if (busId > 0)
      {
        auto *busFilters = player.get()->findBusData(busId);
        if (busFilters == nullptr)
        {
          return busIdNotFound;
        }
        else
        {
          busFilters->filters.setFilterParams(handle > 0 ? handle : busFilters->handle, filterType, attributeId, value);
        }
      }
      else if (handle > 0)
      {
        auto const &s = player.get()->findByHandle(handle);
        if (s == nullptr)
        {
          return soundHandleNotFound;
        }
        else
        {
          s->filters.get()->setFilterParams(handle, filterType, attributeId, value);
        }
      }
    }
    return PlayerErrors::noError;
  }

  /// Get the effect parameter with id [attributeId] of [filterType].
  ///
  /// [handle] the handle to get the filter to. If equal to 0, it gets the global
  /// filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to modify a param. Returns the value of param or
  /// 9999.0 if the filter is not found.
  FFI_PLUGIN_EXPORT enum PlayerErrors getFilterParams(unsigned int handle,
                                                      unsigned int busId,
                                                      enum FilterType filterType,
                                                      int attributeId,
                                                      float *filterValue)
  {
    *filterValue = 9999.0f;
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    /// If [handle] == 0 get the parameter from global filters else from
    /// the sound which owns [handle].
    if (handle == 0 && busId == 0)
    {
      *filterValue =
          player.get()->mFilters.getFilterParams(handle, filterType, attributeId);
      return noError;
    }
    else
    {
      if (busId > 0)
      {
        auto *busFilters = player.get()->findBusData(busId);
        if (busFilters == nullptr)
        {
          return busIdNotFound;
        }
        else
        {
          *filterValue =
              busFilters->filters.getFilterParams(handle > 0 ? handle : busFilters->handle, filterType, attributeId);
          if (!(isnormal(*filterValue) || isnan(*filterValue)))
            return filterParameterGetError;
          if (*filterValue == 9999.0f)
            return filterNotFound;
          return noError;
        }
      }
      else
      {
        auto const &s = player.get()->findByHandle(handle);
        if (s == nullptr)
          return soundHandleNotFound;
        else
        {
          *filterValue =
              s->filters.get()->getFilterParams(handle, filterType, attributeId);
          if (!(isnormal(*filterValue) || isnan(*filterValue)))
            return filterParameterGetError;
          if (*filterValue == 9999.0f)
            return filterNotFound;
          return noError;
        }
      }
    }
    return PlayerErrors::noError;
  }

  /// Fades a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0, it fades
  /// the global filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [to] value the attribute should go in [time] duration.
  /// [time] the fade slope duration. Returns [PlayerErrors.noError] if
  /// no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  fadeFilterParameter(unsigned int handle, unsigned int busId, enum FilterType filterType,
                      int attributeId, float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (handle == 0 && busId == 0)
    {
      player.get()->mFilters.fadeFilterParameter(handle, filterType, attributeId,
                                                 to, time);
    }
    else
    {
      if (busId > 0)
      {
        auto *busFilters = player.get()->findBusData(busId);
        if (busFilters == nullptr)
        {
          return busIdNotFound;
        }
        else
        {
          busFilters->filters.fadeFilterParameter(handle > 0 ? handle : busFilters->handle,
                                                  filterType, attributeId, to,
                                                  time);
        }
      }
      else
      {
        auto const &s = player.get()->findByHandle(handle);
        if (s == nullptr)
        {
          return soundHandleNotFound;
        }
        else
        {
          s->filters.get()->fadeFilterParameter(handle, filterType, attributeId, to,
                                                time);
        }
      }
    }
    return noError;
  }

  /// Oscillate a parameter of a filter.
  ///
  /// [handle] the handle of the voice to apply the fade. If equal to 0, it fades
  /// the global filter.
  /// [busId] the bus to check the filter.
  /// If both [handle] and [busId] are =0 this function searches in the global filters.
  /// [filterType] filter to modify a param.
  /// [attributeId] the attribute index to fade.
  /// [from] the starting value the attribute sould start to oscillate.
  /// [to] the ending value the attribute sould end to oscillate.
  /// [time] the fade slope duration.
  /// Returns [PlayerErrors.noError] if no errors.
  FFI_PLUGIN_EXPORT enum PlayerErrors
  oscillateFilterParameter(unsigned int handle, unsigned int busId, enum FilterType filterType,
                           int attributeId, float from, float to, float time)
  {
    if (player.get() == nullptr || !player.get()->isInited())
      return backendNotInited;
    if (handle == 0 && busId == 0)
    {
      player.get()->mFilters.oscillateFilterParameter(
          handle, filterType, attributeId, from, to, time);
    }
    else
    {
      if (busId > 0)
      {
        auto *busFilters = player.get()->findBusData(busId);
        if (busFilters == nullptr)
        {
          return busIdNotFound;
        }
        else
        {
          busFilters->filters.oscillateFilterParameter(handle > 0 ? handle : busFilters->handle,
                                                       filterType, attributeId,
                                                       from, to, time);
        }
      }
      else
      {
        auto const &s = player.get()->findByHandle(handle);
        if (s == nullptr)
        {
          return soundHandleNotFound;
        }
        else
        {
          s->filters.get()->oscillateFilterParameter(handle, filterType,
                                                     attributeId, from, to, time);
        }
      }
    }
    return noError;
  }

  /////////////////////////////////////////
  /// 3D audio methods
  /////////////////////////////////////////

  /// play3d() is the 3d version of the play() call
  ///
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [looping] whether to start the sound in looping state.
  /// [loopingStartAt] If looping is enabled, the loop point is, by default,
  /// the start of the stream. The loop start point can be set with this
  /// parameter.
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any
  FFI_PLUGIN_EXPORT PlayerErrors play3d(unsigned int soundHash, unsigned int busId,
                                        float posX, float posY, float posZ,
                                        float velX, float velY, float velZ,
                                        float volume, bool paused,
                                        bool looping, double loopingStartAt,
                                        unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return backendNotInited;

    PlayerErrors result =
        player.get()->play3d(soundHash, *handle, posX, posY, posZ, velX, velY,
                             velZ, volume, paused, busId, looping, loopingStartAt,
                             0);
    return result;
  }

  /// Play a 3D sound with an optional bounded loop region.
  ///
  /// This additive entry point preserves the ABI of [play3d].
  /// [loopingEndAt] If greater than zero, loop before this time. Zero uses the
  /// natural end of the stream.
  FFI_PLUGIN_EXPORT PlayerErrors play3dWithLoopPoints(
      unsigned int soundHash, unsigned int busId,
      float posX, float posY, float posZ,
      float velX, float velY, float velZ,
      float volume, bool paused, bool looping, double loopingStartAt,
      double loopingEndAt, float scale, unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return backendNotInited;

    PlayerErrors result =
        player.get()->play3d(soundHash, *handle, posX, posY, posZ, velX, velY,
                             velZ, volume, paused, busId, looping, loopingStartAt,
                             loopingEndAt, scale);
    return result;
  }

  /// play3dClocked() is the 3d version of the playClocked() call.
  ///
  /// Instead of panning like with the "2d" version of the call, the 3d
  /// version requires 3d position and optionally velocity vector. Like its
  /// 2d version, this one delays the start of the sound based on the
  /// [soundTime] parameter, so that firing off sounds rapidly won't cause
  /// the sounds to "clump" together at the start of the next sound buffer.
  ///
  /// [soundHash] the unique sound hash of a sound
  /// [soundTime] your app's "physics time", in seconds.
  /// [posX], [posY], [posZ] are the audio source position coordinates.
  /// [velX], [velY], [velZ] are the audio source velocity.
  /// [volume] 1.0f full volume
  /// [busId] the bus ID to play the sound on. 0 means the main engine.
  /// [handle] pointer to the handle for this new sound
  /// Return the error if any and a new [handle] of this sound
  FFI_PLUGIN_EXPORT PlayerErrors play3dClocked(
      unsigned int soundHash, double soundTime, unsigned int busId,
      float posX, float posY, float posZ,
      float velX, float velY, float velZ,
      float volume, float scale, bool looping,
      double loopingStartAt, double loopingEndAt,
      unsigned int *handle)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return backendNotInited;

    PlayerErrors result =
        player.get()->play3dClocked(soundHash, *handle, soundTime,
                                    posX, posY, posZ, velX, velY, velZ,
                                    volume, busId, scale, looping,
                                    loopingStartAt, loopingEndAt);
    return result;
  }

  /// You can set and get the current value of the speed of
  /// sound width the get3dSoundSpeed() and set3dSoundSpeed() functions.
  /// The speed of sound is used to calculate doppler effects in
  /// addition to the distance delay.

  /// Since SoLoud has no knowledge of the scale of your coordinates,
  /// you may need to adjust the speed of sound for these effects
  /// to work correctly. The default value is 343, which assumes
  /// that your world coordinates are in meters (where 1 unit is 1 meter),
  /// and that the environment is dry air at around 20 degrees Celsius.
  FFI_PLUGIN_EXPORT void set3dSoundSpeed(float speed)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSoundSpeed(speed);
    player.get()->update3dAudio();
  }

  /// Get the sound speed
  FFI_PLUGIN_EXPORT float get3dSoundSpeed()
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return 0.0f;
    return player.get()->get3dSoundSpeed();
  }

  /// You can set the position, at-vector, up-vector and velocity
  /// parameters of the 3d audio listener with one call
  FFI_PLUGIN_EXPORT void
  set3dListenerParameters(float posX, float posY, float posZ, float atX,
                          float atY, float atZ, float upX, float upY, float upZ,
                          float velocityX, float velocityY, float velocityZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dListenerParameters(posX, posY, posZ, atX, atY, atZ, upX,
                                          upY, upZ, velocityX, velocityY,
                                          velocityZ);
    player.get()->update3dAudio();
  }

  /// You can set the position parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerPosition(float posX, float posY,
                                               float posZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dListenerPosition(posX, posY, posZ);
    player.get()->update3dAudio();
  }

  /// You can set the "at" vector parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerAt(float atX, float atY, float atZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dListenerAt(atX, atY, atZ);
    player.get()->update3dAudio();
  }

  /// You can set the "up" vector parameter of the 3d audio listener
  FFI_PLUGIN_EXPORT void set3dListenerUp(float upX, float upY, float upZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dListenerUp(upX, upY, upZ);
    player.get()->update3dAudio();
  }

  /// You can set the listener's velocity vector parameter
  FFI_PLUGIN_EXPORT void set3dListenerVelocity(float velocityX, float velocityY,
                                               float velocityZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dListenerVelocity(velocityX, velocityY, velocityZ);
    player.get()->update3dAudio();
  }

  /// You can set the position and velocity parameters of a live
  /// 3d audio source with one call
  FFI_PLUGIN_EXPORT void set3dSourceParameters(unsigned int handle, float posX,
                                               float posY, float posZ,
                                               float velocityX, float velocityY,
                                               float velocityZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourceParameters(handle, posX, posY, posZ, velocityX,
                                        velocityY, velocityZ);
    player.get()->update3dAudio();
  }

  /// You can set the position parameters of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourcePosition(unsigned int handle, float posX,
                                             float posY, float posZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourcePosition(handle, posX, posY, posZ);
    player.get()->update3dAudio();
  }

  /// You can set the velocity parameters of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceVelocity(unsigned int handle, float velocityX,
                                             float velocityY, float velocityZ)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
    player.get()->update3dAudio();
  }

  /// You can set the minimum and maximum distance parameters
  /// of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceMinMaxDistance(unsigned int handle,
                                                   float minDistance,
                                                   float maxDistance)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
    player.get()->update3dAudio();
  }

  /// You can change the attenuation model and rolloff factor parameters of
  /// a live 3d audio source.
  /// The default values are NO_ATTENUATION and 1.
  ///
  /// NO_ATTENUATION 	        No attenuation
  /// INVERSE_DISTANCE 	    Inverse distance attenuation model
  /// LINEAR_DISTANCE 	    Linear distance attenuation model
  /// EXPONENTIAL_DISTANCE 	Exponential distance attenuation model
  FFI_PLUGIN_EXPORT void set3dSourceAttenuation(unsigned int handle,
                                                unsigned int attenuationModel,
                                                float attenuationRolloffFactor)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourceAttenuation(handle, attenuationModel,
                                         attenuationRolloffFactor);
    player.get()->update3dAudio();
  }

  /// You can change the doppler factor of a live 3d audio source
  FFI_PLUGIN_EXPORT void set3dSourceDopplerFactor(unsigned int handle,
                                                  float dopplerFactor)
  {
    if (player.get() == nullptr || !player.get()->isInited() ||
        player.get()->getSoundsCount() == 0)
      return;
    player.get()->set3dSourceDopplerFactor(handle, dopplerFactor);
    player.get()->update3dAudio();
  }

  /////////////////////////////////////////
  /// waveform audio data
  /////////////////////////////////////////

  FFI_PLUGIN_EXPORT enum ReadSamplesErrors
  readSamplesFromFile(const char *filePath, float startTime, float endTime,
                      unsigned long numSamplesNeeded, bool average,
                      float *pSamples)
  {
    return Waveform::readSamples(filePath, nullptr, 0, startTime, endTime,
                                 numSamplesNeeded, average, pSamples);
  }

  FFI_PLUGIN_EXPORT enum ReadSamplesErrors
  readSamplesFromMem(const unsigned char *buffer, unsigned long dataSize,
                     float startTime, float endTime,
                     unsigned long numSamplesNeeded, bool average,
                     float *pSamples)
  {
    return Waveform::readSamples(nullptr, buffer, dataSize, startTime, endTime,
                                 numSamplesNeeded, average, pSamples);
  }

  /////////////////////////////////////////
  /// Mixing Bus
  /// https://solhsa.com/soloud/mixbus.html
  /// https://solhsa.com/soloud/soloud_20200207.html#mixing-bus
  ///
  /// A mixing bus is a special audio source that plays other audio sources
  /// through it. Useful for grouped volume control, per-bus filtering,
  /// and per-bus visualization (FFT/wave). Busses can also be nested.
  /// Only one instance of a bus can play at a time.
  /// Busses are protected by default and marked as "must tick".
  /////////////////////////////////////////

  /// Create a new mixing bus.
  /// Returns a unique bus ID (>0) to reference this bus in other calls.
  FFI_PLUGIN_EXPORT unsigned int createBus()
  {
    if (player.get() == nullptr)
      return 0;
    return player.get()->createBus();
  }

  /// Destroy a mixing bus by its ID.
  /// Does not stop voices that were playing through the bus.
  FFI_PLUGIN_EXPORT void destroyBus(unsigned int busId)
  {
    if (player.get() == nullptr)
      return;
    player.get()->destroyBus(busId);
  }

  /// Play the bus itself on the main SoLoud engine so it becomes audible.
  /// You must call this before sounds routed through the bus can be heard.
  ///
  /// [busId] the bus ID returned by createBus.
  /// [volume] playback volume (1.0 = full).
  /// [paused] whether to start paused.
  /// [handle] set to the voice handle of the bus, or 0 on error.
  /// Returns [PlayerErrors.noError] if success, [PlayerErrors.backendNotInited]
  /// if the engine is not initialized, [PlayerErrors.busIdNotFound] if [busId]
  /// is unknown, [PlayerErrors.failedToStartPlayback] if no voice could be
  /// created for the bus. When [paused] is false the output device is started
  /// asynchronously after the bus voice exists, so this never reports
  /// [PlayerErrors.audioDeviceFailedToStart].
  ///
  /// Note: to play a sound through a bus, the play() function is used with the
  /// bus ID as an argument. See play() for more information.
  FFI_PLUGIN_EXPORT enum PlayerErrors busPlayOnEngine(unsigned int busId,
                                                      float volume, bool paused,
                                                      unsigned int *handle)
  {
    // Zero it before the guard below: `Player::busPlayOnEngine()` always sets
    // it, but this early return would otherwise leave the caller's buffer
    // untouched. On the web that buffer comes from `_malloc()`, which does not
    // zero, so Dart would read uninitialized memory as a handle.
    *handle = 0;
    if (player.get() == nullptr)
      return backendNotInited;
    return player.get()->busPlayOnEngine(busId, volume, paused, *handle);
  }

  /// Set the number of output channels for the bus (default is 2 = stereo).
  ///
  /// [busId] the bus ID.
  /// [channels] number of channels (1 = mono, 2 = stereo, etc.).
  FFI_PLUGIN_EXPORT int busSetChannels(unsigned int busId,
                                       unsigned int channels)
  {
    if (player.get() == nullptr)
      return -1;
    return player.get()->busSetChannels(busId, channels);
  }

  /// Enable or disable visualization data gathering for this bus.
  /// Must be enabled before calling busCalcFFT, busGetWave,
  /// or busGetApproximateVolume.
  ///
  /// [busId] the bus ID.
  /// [enable] true to enable, false to disable.
  FFI_PLUGIN_EXPORT void busSetVisualizationEnable(unsigned int busId,
                                                   bool enable)
  {
    if (player.get() == nullptr)
      return;
    player.get()->busSetVisualizationEnable(busId, enable);
  }

  /// Calculate and return 256 floats of FFT data for this bus.
  /// The data ranges from low to high frequencies.
  /// Visualization must be enabled first with busSetVisualizationEnable.
  ///
  /// [busId] the bus ID.
  /// Returns a pointer to 256 floats, or nullptr if the bus is not found.
  FFI_PLUGIN_EXPORT float *busCalcFFT(unsigned int busId)
  {
    if (player.get() == nullptr)
      return nullptr;
    return player.get()->busCalcFFT(busId);
  }

  /// Get 256 samples of wave data currently playing through this bus.
  /// Visualization must be enabled first with busSetVisualizationEnable.
  ///
  /// [busId] the bus ID.
  /// Returns a pointer to 256 floats, or nullptr if the bus is not found.
  FFI_PLUGIN_EXPORT float *busGetWave(unsigned int busId)
  {
    if (player.get() == nullptr)
      return nullptr;
    return player.get()->busGetWave(busId);
  }

  /// Get the approximate output volume for a specific channel of this bus.
  /// Useful for VU meters or level indicators.
  /// Visualization must be enabled first.
  ///
  /// [busId] the bus ID.
  /// [channel] the output channel index (0 = left, 1 = right, etc.).
  /// Returns the approximate volume, or 0 if the bus is not found.
  FFI_PLUGIN_EXPORT float busGetApproximateVolume(unsigned int busId,
                                                  unsigned int channel)
  {
    if (player.get() == nullptr)
      return 0.0f;
    return player.get()->busGetApproximateVolume(busId, channel);
  }

  /// Move a live voice (identified by its handle) into this bus.
  /// The voice will be reparented so it plays through the bus.
  /// Useful for dynamically routing sounds in/out of filtered busses.
  ///
  /// [busId] the bus ID.
  /// [voiceHandle] handle of the voice to annex.
  FFI_PLUGIN_EXPORT void busAnnexSound(unsigned int busId,
                                       unsigned int voiceHandle)
  {
    if (player.get() == nullptr)
      return;
    player.get()->busAnnexSound(busId, voiceHandle);
  }

  /// Get the number of voices currently playing through this bus.
  ///
  /// [busId] the bus ID.
  /// Returns the active voice count, or 0 if the bus is not found.
  FFI_PLUGIN_EXPORT unsigned int busGetActiveVoiceCount(unsigned int busId)
  {
    if (player.get() == nullptr)
      return 0;
    return player.get()->busGetActiveVoiceCount(busId);
  }

#ifdef __cplusplus
}
#endif

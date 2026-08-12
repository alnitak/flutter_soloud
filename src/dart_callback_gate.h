#pragma once

#ifndef FLUTTER_SOLOUD_DART_CALLBACK_GATE_H
#define FLUTTER_SOLOUD_DART_CALLBACK_GATE_H

#include <cstdint>
#include <mutex>
#include <shared_mutex>

/// One gate that every Dart callable native code holds must pass through.
///
/// Retiring a Dart trampoline is not "store nullptr over the pointer". A
/// thread that has already loaded the pointer will still call it, and the
/// isolate that owns it may be gone by then — a hot restart or a destroyed
/// FlutterEngine takes the isolate away without native code being asked first.
/// Retirement therefore has to guarantee two things at once:
///
///   * no invocation is in flight when it returns, and
///   * no invocation can start afterwards.
///
/// Only a lock gives that. Invocation takes it shared (so the audio thread
/// never queues behind an unrelated callback), retirement takes it exclusive.
///
/// The second problem is reach. The process-global callables (voice ended,
/// file loaded, state changed, mixer output) are a handful of pointers, but
/// every BufferStream and PullBufferStream holds its own — buffering,
/// metadata, more-data-needed, audio-duration — and reaching those means
/// walking Player's sound list under `sounds_mutex`, which is held across
/// decoding work. A lifecycle hook runs on Android's platform thread and
/// cannot wait for that. So retirement does not visit them: it invalidates a
/// *generation* instead. Every registration records the generation live when
/// it was made, every invocation checks its registration's generation against
/// the live one, and retiring is a single store that makes every callable in
/// the process inert at once, however many sources exist.
///
/// The generation also settles the case a bare flag cannot: a source
/// registered by an engine that has since been retired stays dead even after a
/// *replacement* engine publishes its own registration, because the
/// replacement's generation is a new number.
///
/// Lock ordering: this gate is a leaf. Acquire it last, release it first, and
/// never acquire another lock — or invoke anything that re-enters a dispatcher
/// — while holding it. Retirement holds no other lock, so a hook can take it
/// from the platform thread without ever waiting on device or decode work.
namespace dart_callbacks
{

  /// Used where no FlutterEngine lifecycle exists: the web build, the desktop
  /// embedders, and any host that does not run the Android plugin.
  constexpr int64_t kNoEngineId = -1;

  /// The generation of a registration that was never made, or has been retired.
  /// Never live, so it can be the default for a source that has no callbacks.
  constexpr uint64_t kNoGeneration = 0;

  namespace detail
  {
    struct GateState
    {
      std::shared_mutex mutex;
      /// The only generation allowed to run, or kNoGeneration when retired.
      uint64_t liveGeneration = kNoGeneration;
      /// Monotonic source of generations. Never reused.
      uint64_t lastGeneration = kNoGeneration;
      /// Which FlutterEngine published the live registration.
      int64_t ownerEngineId = kNoEngineId;
    };

    /// Deliberately never destroyed: detached teardown workers and the audio
    /// thread can outlive main(), and locking a destroyed mutex is worse than
    /// leaking one small object.
    inline GateState &gate()
    {
      static GateState *state = new GateState();
      return *state;
    }
  } // namespace detail

#ifdef __EMSCRIPTEN__

  /// The web has nothing for this gate to protect, so it compiles away.
  ///
  /// There is no Dart FFI trampoline on the web. A callback is a JS function
  /// parked on `globalThis` under its sound hash, and the dispatchers reach it
  /// through EM_ASM, which resolves the name at call time and does nothing when
  /// it is absent — the pointer native code holds is the sentinel `1`, never an
  /// address. Nothing can dangle, so there is nothing to retire: the web build
  /// has no FlutterEngine lifecycle hooks and never claims a generation, which
  /// would leave every source stuck at kNoGeneration and every callback dead.
  ///
  /// It is also the wrong lock to hold there. The gate would span an
  /// EM_ASM → JS → Dart call, and the web callback model re-enters in ways a
  /// `NativeCallable.listener` does not.
  class InvocationPass
  {
  public:
    bool isLive(uint64_t) const { return true; }
  };

#else

  /// Held for the duration of one Dart trampoline invocation.
  ///
  /// Construct it *before* loading the callable pointer, and keep it alive
  /// across the call.
  class InvocationPass
  {
  public:
    InvocationPass()
        : mLock(detail::gate().mutex),
          mLiveGeneration(detail::gate().liveGeneration) {}

    /// Whether a callable registered at [registrationGeneration] may run.
    bool isLive(uint64_t registrationGeneration) const
    {
      return registrationGeneration != kNoGeneration &&
             registrationGeneration == mLiveGeneration;
    }

  private:
    // Declaration order is load-bearing: the lock must be taken before the
    // live generation is read.
    std::shared_lock<std::shared_mutex> mLock;
    uint64_t mLiveGeneration;
  };

#endif // __EMSCRIPTEN__

  /// Exclusive access for publishing or retiring registrations.
  ///
  /// Scoped so the caller can publish or null its own pointers inside the same
  /// critical section: a check that releases the gate before acting can be
  /// overtaken by the retirement it just checked against.
  class Registration
  {
  public:
    Registration() : mLock(detail::gate().mutex) {}

    /// Publish a registration owned by [ownerEngineId] and return its
    /// generation. Any earlier registration — and every source that recorded
    /// its generation — is left permanently inert.
    uint64_t claim(int64_t ownerEngineId)
    {
      detail::GateState &state = detail::gate();
      state.ownerEngineId = ownerEngineId;
      state.liveGeneration = ++state.lastGeneration;
      return state.liveGeneration;
    }

    /// Whether [engineId] owns the live registration. A caller with no engine
    /// lifecycle (kNoEngineId) matches a registration published the same way.
    bool isOwnedBy(int64_t engineId) const
    {
      const detail::GateState &state = detail::gate();
      return state.liveGeneration != kNoGeneration &&
             state.ownerEngineId == engineId;
    }

    /// Retire the live registration if [engineId] owns it. Returns false when
    /// somebody else does, so a detaching engine never retires another's.
    bool retire(int64_t engineId)
    {
      if (!isOwnedBy(engineId))
        return false;

      retireAll();
      return true;
    }

    /// Retire whatever is live, whoever owns it.
    void retireAll()
    {
      detail::GateState &state = detail::gate();
      state.liveGeneration = kNoGeneration;
      state.ownerEngineId = kNoEngineId;
    }

    uint64_t generation() const { return detail::gate().liveGeneration; }

  private:
    std::unique_lock<std::shared_mutex> mLock;
  };

  /// The generation a source-owned registration must record when it stores its
  /// callables. kNoGeneration when nothing is live, which leaves the source
  /// inert — correct, because there is no isolate to call into.
  inline uint64_t currentGeneration()
  {
    std::shared_lock<std::shared_mutex> lock(detail::gate().mutex);
    return detail::gate().liveGeneration;
  }

} // namespace dart_callbacks

#endif // FLUTTER_SOLOUD_DART_CALLBACK_GATE_H

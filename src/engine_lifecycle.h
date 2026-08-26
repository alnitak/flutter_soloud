#pragma once

#ifndef FLUTTER_SOLOUD_ENGINE_LIFECYCLE_H
#define FLUTTER_SOLOUD_ENGINE_LIFECYCLE_H

#include <stdbool.h>
#include <stdint.h>

#ifndef SOLOUD_COMMON_H
#include "soloud_common.h"
#endif

/// The FlutterEngine lifecycle entry points, implemented in `bindings.cpp`.
///
/// They are declared here rather than in each caller because they are reached
/// from three directions: Dart over FFI, the Android plugin over JNI, and the
/// iOS plugin from Objective-C++. All three drive the *same* lifecycle claim
/// and generation state — the embedder observes when a FlutterEngine appears
/// and disappears, and this shared implementation decides what that means for
/// the process-global engine.
///
/// Every one of them is exported with default visibility: the iOS plugin may
/// be compiled into a dynamic framework (`use_frameworks!`) that resolves these
/// at load time against the copy force-loaded into the app binary, and a hidden
/// or stripped symbol would fail there rather than at build time.
///
/// About engine ids: they are opaque, and only unique among *live* engines. On
/// iOS the id is the FlutterEngine's own address, which the allocator reuses
/// once an engine is gone. That is safe here because every id-only comparison
/// happens synchronously while the engine is still alive — a detach runs inside
/// that engine's dealloc — and everything asynchronous carries the generation
/// as well, which is never reused.
#ifdef __cplusplus
extern "C"
{
#endif

  /// Claim the native engine for [owner_engine_id] before its initialization
  /// is dispatched, and invalidate any teardown queued by a previous engine.
  /// -1 means "no engine lifecycle available on this platform".
  FFI_PLUGIN_EXPORT void prepareEngineInit(int64_t owner_engine_id);

  /// The epoch a prepare request must quote to be accepted. Read it before
  /// starting a claim that cannot be taken synchronously.
  FFI_PLUGIN_EXPORT uint64_t currentEngineShutdownEpoch(void);

  /// Take the claim prepareEngineInit() would take, but only if no shutdown has
  /// been requested since [shutdown_epoch] was read. Returns whether it did.
  ///
  /// For callers that decide to initialize at one moment and can only claim at
  /// a later one — iOS hands the claim to its plugin over a method channel, and
  /// `deinit()` can run while Dart is suspended in between. A claim that lands
  /// after the teardown that superseded it would lower the shutdown flag and
  /// leave ownership recorded for an engine that is already gone.
  FFI_PLUGIN_EXPORT bool prepareEngineInitForRequest(int64_t owner_engine_id,
                                                     uint64_t shutdown_epoch);

  /// Retire every Dart callable owned by [engine_id] — the process-global ones
  /// and the per-source ones alike. Returns false when a different engine owns
  /// the live registration. Takes one uncontended lock and performs no blocking
  /// work, so it is safe to call from a platform/UI thread.
  FFI_PLUGIN_EXPORT bool clearDartCallbackRegistrationsForEngine(
      int64_t engine_id);

  /// Retire [engine_id]'s callables and, if it still owns the native engine,
  /// queue the teardown on a worker thread. Returns whether the teardown was
  /// accepted. Never blocks: safe to call from a platform/UI thread.
  FFI_PLUGIN_EXPORT bool requestEngineTeardownForEngine(int64_t engine_id);

  /// Invoked as a Dart NativeFinalizer when an isolate is destroyed/restarted.
  FFI_PLUGIN_EXPORT void retireDartCallbacksFinalizer(void *token);

#ifdef __cplusplus
}
#endif

#endif // FLUTTER_SOLOUD_ENGINE_LIFECYCLE_H

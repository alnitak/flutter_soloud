import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';

import 'common.dart';

/// Test the init/deinit/re-init lifecycle that PR #444 fixes.
///
/// After a hot restart the native engine can survive while the Dart isolate's
/// callback bindings are gone.  [SoLoud.isInitialized] must stay `false` until
/// callbacks are rebound by a fresh [SoLoud.init] call.  We can't trigger a
/// real hot restart from an integration test, but we CAN exercise the same
/// code path: call `init()` when the native player is already alive (which
/// `init()` treats as the hot-restart recovery branch).
Future<OutputBuffer> testHotRestartLifecycle() async {
  final buf = OutputBuffer();

  // ── 1. Fresh init ───────────────────────────────────────────────────────
  await SoLoud.instance.init();
  assert(
    SoLoud.instance.isInitialized,
    'isInitialized must be true after init()',
  );
  assert(
    SoLoudController().soLoudFFI.isInited(),
    'native isInited must be true after init()',
  );
  buf.writeln('1. Fresh init: OK');

  // ── 2. deinit clears both sides ─────────────────────────────────────────
  SoLoud.instance.deinit();
  assert(
    !SoLoud.instance.isInitialized,
    'isInitialized must be false after deinit()',
  );
  assert(
    !SoLoudController().soLoudFFI.isInited(),
    'native isInited must be false after deinit()',
  );
  buf.writeln('2. deinit: OK');

  // ── 3. Clean re-init after full deinit ───────────────────────────────────
  await SoLoud.instance.init();
  assert(
    SoLoud.instance.isInitialized,
    'isInitialized must be true after re-init()',
  );
  buf.writeln('3. Clean re-init: OK');

  // ── 4. Hot-restart recovery: init() while native is still alive ─────────
  //    This is the actual hot-restart code path (soloud.dart ~line 298).
  //    When init() detects native is already initialized, it calls
  //    clearDartCallbackRegistrations() + deinit() internally, then
  //    re-initializes everything with fresh callbacks.
  await SoLoud.instance.init();
  assert(
    SoLoud.instance.isInitialized,
    'isInitialized must be true after hot-restart recovery init()',
  );
  assert(
    SoLoudController().soLoudFFI.isInited(),
    'native isInited must be true after hot-restart recovery init()',
  );
  buf.writeln('4. Hot-restart recovery (init while native alive): OK');

  // ── 5. Callbacks work after hot-restart recovery ────────────────────────
  //    Load a sound and play it — this exercises the voice-ended callback
  //    path that would crash with stale pointers after a real hot restart.
  final sound = await loadAsset();
  final handle = SoLoud.instance.play(sound);

  // The handle must be valid.
  assert(
    SoLoudController().soLoudFFI.getIsValidVoiceHandle(handle),
    'handle should be valid after play()',
  );

  // Let it play briefly, then stop — the voice-ended callback fires.
  await delay(200);
  await SoLoud.instance.stop(handle);
  await delay(100);

  assert(
    !SoLoudController().soLoudFFI.getIsValidVoiceHandle(handle),
    'handle should be invalid after stop()',
  );
  buf.writeln('5. Callbacks work after hot-restart recovery: OK');

  // ── 6. Rapid init/deinit/init cycling ───────────────────────────────────
  SoLoud.instance.deinit();
  for (var i = 0; i < 5; i++) {
    await SoLoud.instance.init();
    assert(SoLoud.instance.isInitialized, 'cycle $i: should be initialized');
    SoLoud.instance.deinit();
    assert(
      !SoLoud.instance.isInitialized,
      'cycle $i: should not be initialized',
    );
  }
  buf.writeln('6. Rapid init/deinit cycling (5x): OK');

  debugPrint(buf.toString());
  return buf;
}

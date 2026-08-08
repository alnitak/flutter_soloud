import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';

import 'common.dart';

/// Wall-clock budget for a single `changeDevice()` call.
///
/// A swap closes one stream and opens another: tens of milliseconds in
/// practice, a few hundred on a slow emulator. What matters is the number this
/// excludes. Holding SoLoud's audio-thread mutex across the swap starves the
/// audio callback, and on AAudio's legacy (non-MMAP) path a stream only reports
/// STARTED once its first data callback has run — so `ma_device_start()` sits
/// in `AAudioStream_waitForStateChange()` for its full 5s timeout before it
/// fails.
/// Anything near that is the mutex regression, not a slow device.
const _changeDeviceBudget = Duration(seconds: 2);

/// Test playback device enumeration and switching.
Future<OutputBuffer> testPlaybackDevices() async {
  final strBuf = OutputBuffer();
  await initialize();

  // List all playback devices
  final devices = SoLoud.instance.listPlaybackDevices();
  strBuf.writeln('Found ${devices.length} playback device(s)');

  for (final device in devices) {
    strBuf.writeln(
      '  Device: ${device.name}, ID: ${device.id}, '
      'IsDefault: ${device.isDefault}',
    );
  }

  assert(devices.isNotEmpty, 'Should have at least one playback device');

  // Load and play a looping sound to test device switching
  // The sound must keep playing throughout all device switches
  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  // Enable looping so the sound continues playing during device switches
  SoLoud.instance.play(sound);

  // Regression: calling `changeDevice()` without an argument selects the
  // current system default output device. It used to throw
  // `SoLoudNoPlaybackDevicesFoundCppException` because the native `-1`
  // sentinel was promoted to a huge unsigned value when compared against the
  // device count.
  // Note: this is also the first time this path is reachable on Web, where it
  // tears down and re-initializes the WebAudio device.
  final toDefault = _timeChangeDevice('Switching to the default device');
  assert(
    await _isPlaybackUsable(sound),
    'The engine should still play after switching to the default device',
  );
  strBuf.writeln(
    'Switched to the default device in ${toDefault.inMilliseconds}ms, '
    'playback still usable',
  );

  // Regression: an enumerated device still works. Only one device is required
  // because CI/desktop machines commonly expose a single one.
  final toEnumerated = _timeChangeDevice(
    'Switching to device "${devices.first.name}"',
    newDevice: devices.first,
  );
  assert(
    await _isPlaybackUsable(sound),
    'The engine should still play after switching to an enumerated device',
  );
  strBuf.writeln(
    'Switched to device "${devices.first.name}" in '
    '${toEnumerated.inMilliseconds}ms, playback still usable',
  );

  // Invalid native selectors are rejected before the current device is
  // touched, so they must not disrupt the playback started above.
  // `PlaybackDevice` cannot express these IDs, hence the direct binding call.
  final belowSentinel = SoLoudController().soLoudFFI.changeDevice(-2);
  assert(
    belowSentinel == PlayerErrors.invalidParameter,
    'A device ID below -1 should be rejected as an invalid parameter, '
    'got $belowSentinel',
  );
  final outOfRange = SoLoudController().soLoudFFI.changeDevice(9999);
  assert(
    outOfRange == PlayerErrors.noPlaybackDevicesFound,
    'An out of range device ID should report no playback devices found, '
    'got $outOfRange',
  );
  assert(
    await _isPlaybackUsable(sound),
    'Rejected device IDs should leave the current device untouched',
  );
  strBuf.writeln('Invalid device IDs rejected without disrupting playback');

  // Swap repeatedly while the mixer is actually running. Nothing serializes the
  // audio callback against the swap any more: `ma_device_uninit()` alone is
  // responsible for quiescing the callback before its stream is closed, and
  // `Soloud::mix()` takes the audio mutex itself. If that ordering were not
  // enough, a live voice across back-to-back swaps is what would expose it.
  final looped = SoLoud.instance.play(sound, looping: true);
  assert(
    SoLoud.instance.getIsValidVoiceHandle(looped),
    'The looping voice used for the swap stress test should start',
  );
  var slowestSwap = Duration.zero;
  for (var i = 0; i < 10; i++) {
    final elapsed = _timeChangeDevice('Stress swap $i');
    if (elapsed > slowestSwap) slowestSwap = elapsed;
    assert(
      SoLoud.instance.getIsValidVoiceHandle(looped),
      'The looping voice should survive swap $i: a device change replaces the '
      'output device, it does not touch voices',
    );
    await delay(100);
  }
  assert(
    SoLoud.instance.getActiveVoiceCount() > 0,
    'The engine should still be mixing after 10 device swaps',
  );
  strBuf.writeln(
    '10 back-to-back swaps under a live voice, slowest '
    '${slowestSwap.inMilliseconds}ms',
  );

  // Aim a swap at the deferred engine pause. `Player`'s pause scheduler stops
  // the audio device from its own thread ~500ms (kPauseEngineDelayMs) after the
  // last voice ends, so it is the one device operation an app can drive
  // concurrently with a swap without any OS lifecycle event. Both act on the
  // same `ma_device`, and mid-swap there is no device at all — a start or stop
  // landing there is operating on a torn struct.
  //
  // Note this is a probe, not a proof: it can only make the collision likely,
  // and on web there is no scheduler thread at all (the wasm build pauses
  // inline). A green run is evidence, not a guarantee of correct locking.
  await SoLoud.instance.stop(looped);
  for (var i = 0; i < 5; i++) {
    await delay(450);
    _timeChangeDevice('Swap $i racing the deferred engine pause');
    assert(
      await _isPlaybackUsable(sound),
      'The engine should still play after swap $i raced the engine pause',
    );
  }
  strBuf.writeln('5 swaps aimed at the deferred engine pause window survived');

  // On desktop platforms, we can test changing devices
  // On mobile and web, there's typically only the default device
  // Note: not all output devices can be heard.
  if (!kIsWeb && devices.length > 1) {
    for (final device in devices) {
      strBuf.writeln('Testing device: ${device.name}');
      debugPrint('Testing device: ${device.name}');
      _timeChangeDevice('Switching to "${device.name}"', newDevice: device);

      await delay(3000);
    }
  } else {
    strBuf.writeln(
      'Skipping device switching test (web/mobile or single device)',
    );
  }

  deinit();

  // Regression: initializing with an explicit device. `init()` used to build
  // the device list inside an `if` block and hand the native engine a pointer
  // into it, so by the time miniaudio opened the device the vector had already
  // been destroyed and the `ma_device_id` was freed memory.
  //
  // A plain green run here proves little: freed storage often still holds the
  // right bytes, so the buggy version could return `noError` and play fine.
  // Run this under AddressSanitizer to actually exercise it — against the
  // pre-fix code ASan reports a heap-use-after-free, a 256-byte read inside
  // `ma_device_init()` of storage freed by the device vector's destructor in
  // `Player::init()`. After the fix it is silent.
  await SoLoud.instance.init(device: devices.first);
  assert(
    SoLoud.instance.isInitialized,
    'The engine should initialize on the explicitly selected device '
    '"${devices.first.name}"',
  );
  if (!kIsWeb) {
    SoLoud.instance.setGlobalVolume(0.2);
  }

  // The previous `deinit()` invalidated every source, so reload before use.
  final reloaded =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  assert(
    await _isPlaybackUsable(reloaded),
    'The engine should play after initializing on an explicit device',
  );
  strBuf.writeln(
    'Initialized on device "${devices.first.name}", playback usable',
  );

  deinit();

  strBuf.writeln('Playback devices tests completed successfully');
  return strBuf;
}

/// Calls `changeDevice()` and reports how long the native call took, asserting
/// it stayed inside [_changeDeviceBudget].
///
/// This cannot catch a true deadlock: `changeDevice()` is a synchronous FFI
/// call on the UI isolate, so once the native side wedges there is no Dart code
/// left to time it out — the app just freezes (the Android ANR). What it does
/// catch is the multi-second stall that precedes that deadlock, which has the
/// same cause and is visible on every platform where the swap then recovers.
Duration _timeChangeDevice(String what, {PlaybackDevice? newDevice}) {
  final stopwatch = Stopwatch()..start();
  SoLoud.instance.changeDevice(newDevice: newDevice);
  stopwatch.stop();
  assert(
    stopwatch.elapsed < _changeDeviceBudget,
    '$what took ${stopwatch.elapsedMilliseconds}ms, over the '
    '${_changeDeviceBudget.inMilliseconds}ms budget. The audio callback is '
    'being starved during the swap — see _changeDeviceBudget.',
  );
  return stopwatch.elapsed;
}

/// Starts a voice and checks the engine handed back a usable handle, then
/// stops it again. Used to confirm the output device survived a change.
Future<bool> _isPlaybackUsable(AudioSource sound) async {
  final handle = SoLoud.instance.play(sound);
  final isValid = SoLoud.instance.getIsValidVoiceHandle(handle);
  if (isValid) {
    await SoLoud.instance.stop(handle);
  }
  return isValid;
}

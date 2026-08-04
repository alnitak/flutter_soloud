import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart';

import 'common.dart';

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
  SoLoud.instance.changeDevice();
  assert(
    await _isPlaybackUsable(sound),
    'The engine should still play after switching to the default device',
  );
  strBuf.writeln('Switched to the default device, playback still usable');

  // Regression: an enumerated device still works. Only one device is required
  // because CI/desktop machines commonly expose a single one.
  SoLoud.instance.changeDevice(newDevice: devices.first);
  assert(
    await _isPlaybackUsable(sound),
    'The engine should still play after switching to an enumerated device',
  );
  strBuf.writeln(
    'Switched to device "${devices.first.name}", playback still usable',
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

  // On desktop platforms, we can test changing devices
  // On mobile and web, there's typically only the default device
  // Note: not all output devices can be heard.
  if (!kIsWeb && devices.length > 1) {
    for (final device in devices) {
      strBuf.writeln('Testing device: ${device.name}');
      debugPrint('Testing device: ${device.name}');
      SoLoud.instance.changeDevice(newDevice: device);

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

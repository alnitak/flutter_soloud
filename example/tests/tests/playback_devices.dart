import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test playback device enumeration and switching.
Future<StringBuffer> testPlaybackDevices() async {
  final strBuf = StringBuffer();
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

  // On desktop platforms, we can test changing devices
  // On mobile and web, there's typically only the default device
  // Note: not all output devices can be heard.
  if (!kIsWeb && devices.length > 1) {
    for (final device in devices) {
      strBuf.writeln('Testing device: ${device.name}');
      debugPrint('Testing device: ${device.name}');
      await SoLoud.instance.changeDevice(newDevice: device);

      await delay(3000);
    }
  } else {
    strBuf.writeln(
      'Skipping device switching test (web/mobile or single device)',
    );
  }

  deinit();

  strBuf.writeln('Playback devices tests completed successfully');
  return strBuf;
}

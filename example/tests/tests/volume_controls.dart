import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test volume controls including setVolume, getVolume,
/// fadeVolume, and oscillateVolume.
Future<OutputBuffer> testVolumeControls() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load and play a sound
  final sound = await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final handle = SoLoud.instance.play(sound, looping: true);

  // Test setVolume and getVolume
  SoLoud.instance.setVolume(handle, 0.5);
  var volume = SoLoud.instance.getVolume(handle);
  assert(
    closeTo(volume, 0.5, 0.01),
    'setVolume/getVolume failed: expected 0.5, got $volume',
  );
  strBuf.writeln('Volume set to 0.5, got: $volume');

  // Test volume at 0
  SoLoud.instance.setVolume(handle, 0);
  volume = SoLoud.instance.getVolume(handle);
  assert(
    closeTo(volume, 0.0, 0.01),
    'setVolume/getVolume failed at 0: expected 0.0, got $volume',
  );
  strBuf.writeln('Volume set to 0.0, got: $volume');

  // Test volume at 1.0
  SoLoud.instance.setVolume(handle, 1);
  volume = SoLoud.instance.getVolume(handle);
  assert(
    closeTo(volume, 1.0, 0.01),
    'setVolume/getVolume failed at 1.0: expected 1.0, got $volume',
  );
  strBuf.writeln('Volume set to 1.0, got: $volume');

  // Reset volume for fade test
  SoLoud.instance.setVolume(handle, 1);

  // Test fadeVolume
  strBuf.writeln('Starting fadeVolume from 1.0 to 0.2 over 300ms');
  SoLoud.instance.fadeVolume(handle, 0.2, const Duration(milliseconds: 300));

  // Wait for fade to complete
  await delay(400);

  volume = SoLoud.instance.getVolume(handle);
  assert(
    closeTo(volume, 0.2, 0.05),
    'fadeVolume failed: expected ~0.2, got $volume',
  );
  strBuf
    ..writeln('After fade, volume: $volume')

    // Test oscillateVolume
    ..writeln('Starting oscillateVolume between 0.3 and 0.7 over 400ms');
  SoLoud.instance.oscillateVolume(
    handle,
    0.3,
    0.7,
    const Duration(milliseconds: 400),
  );

  // Let it oscillate for a bit
  await delay(200);
  final midVolume = SoLoud.instance.getVolume(handle);
  strBuf.writeln('Mid-oscillation volume: $midVolume');

  // Wait for oscillation to complete
  await delay(300);

  // Stop oscillation by setting a fixed volume
  SoLoud.instance.setVolume(handle, 0.5);
  volume = SoLoud.instance.getVolume(handle);
  strBuf.writeln('After oscillation, volume set to: $volume');

  // Cleanup
  await SoLoud.instance.stop(handle);
  deinit();

  strBuf.writeln('Volume controls tests completed successfully');
  return strBuf;
}

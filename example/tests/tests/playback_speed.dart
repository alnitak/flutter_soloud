import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test playback speed controls including setRelativePlaySpeed,
/// fadeRelativePlaySpeed, and oscillateRelativePlaySpeed.
Future<OutputBuffer> testPlaybackSpeed() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load and play a sound
  final sound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  final handle = SoLoud.instance.play(sound, looping: true);

  // Test normal speed
  var speed = SoLoud.instance.getRelativePlaySpeed(handle);
  assert(
    closeTo(speed, 1.0, 0.01),
    'Initial speed should be 1.0, got $speed',
  );
  strBuf.writeln('Initial speed: $speed');

  // Test setting speed to 2x (double speed, higher pitch)
  SoLoud.instance.setRelativePlaySpeed(handle, 2);
  speed = SoLoud.instance.getRelativePlaySpeed(handle);
  assert(
    closeTo(speed, 2.0, 0.01),
    'Speed should be 2.0, got $speed',
  );
  strBuf.writeln('Speed set to 2.0 (double speed): $speed');

  await delay(500);

  // Test half speed (lower pitch)
  SoLoud.instance.setRelativePlaySpeed(handle, 0.5);
  speed = SoLoud.instance.getRelativePlaySpeed(handle);
  assert(
    closeTo(speed, 0.5, 0.01),
    'Speed should be 0.5, got $speed',
  );
  strBuf.writeln('Speed set to 0.5 (half speed): $speed');

  await delay(500);

  // Reset to normal
  SoLoud.instance.setRelativePlaySpeed(handle, 1);
  strBuf.writeln('Speed reset to 1.0');

  await delay(200);

  // Test fadeRelativePlaySpeed
  strBuf.writeln('Fading speed from 1.0 to 1.5 over 400ms');
  SoLoud.instance.fadeRelativePlaySpeed(
    handle,
    1.5,
    const Duration(milliseconds: 400),
  );

  await delay(200);
  final midSpeed = SoLoud.instance.getRelativePlaySpeed(handle);
  strBuf.writeln('Mid-fade speed: $midSpeed');

  await delay(300);
  speed = SoLoud.instance.getRelativePlaySpeed(handle);
  assert(
    closeTo(speed, 1.5, 0.05),
    'After fade, speed should be ~1.5, got $speed',
  );
  strBuf
    ..writeln('After fade, speed: $speed')

    // Test oscillateRelativePlaySpeed
    ..writeln('Oscillating speed between 0.8 and 1.2 over 600ms');
  SoLoud.instance.oscillateRelativePlaySpeed(
    handle,
    0.8,
    1.2,
    const Duration(milliseconds: 600),
  );

  // Let it oscillate
  await delay(300);
  final oscillatingSpeed = SoLoud.instance.getRelativePlaySpeed(handle);
  strBuf.writeln('Mid-oscillation speed: $oscillatingSpeed');

  await delay(400);

  // Cleanup
  await SoLoud.instance.stop(handle);
  deinit();

  strBuf.writeln('Playback speed tests completed successfully');
  return strBuf;
}

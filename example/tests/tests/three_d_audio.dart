// ignore_for_file: avoid_redundant_argument_values

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test 3D audio functionality including positioning, attenuation,
/// and doppler effects.
Future<OutputBuffer> test3dAudio() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load a sound
  final sound = await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  // Play 3D sound at position (10, 0, 0) - positional args
  final handle = SoLoud.instance.play3d(
    sound,
    10, 0, 0, // posX, posY, posZ
    velX: 0,
    velY: 0,
    velZ: 0,
    volume: 1,
    looping: true,
  );
  strBuf.writeln('Playing 3D sound at position (10, 0, 0)');

  // Set listener parameters at origin (positional args, not named)
  SoLoud.instance.set3dListenerParameters(
    0, 0, 0, // pos
    0, 0, 1, // at (looking forward)
    0, 1, 0, // up
    0, 0, 0, // velocity
  );
  strBuf.writeln('Listener set at origin');

  // Test 3D sound speed
  SoLoud.instance.set3dSoundSpeed(343);
  final speed = SoLoud.instance.get3dSoundSpeed();
  assert(
    closeTo(speed, 343, 0.1),
    'set3dSoundSpeed/get3dSoundSpeed failed: expected 343, got $speed',
  );
  strBuf.writeln('3D sound speed: $speed m/s');

  // Wait briefly for playback to stabilize
  await delay(100);

  // Test setting source position (positional args)
  SoLoud.instance.set3dSourcePosition(handle, 5, 0, 0);
  strBuf.writeln('Moved source to position (5, 0, 0)');

  // Test setting source velocity for doppler (positional args)
  SoLoud.instance.set3dSourceVelocity(handle, 10, 0, 0);
  strBuf.writeln('Set source velocity to (10, 0, 0)');

  // Test setting min/max distance for attenuation
  SoLoud.instance.set3dSourceMinMaxDistance(handle, 1, 100);
  strBuf.writeln('Set min/max distance to 1/100');

  // Test attenuation models
  // 0 = NO_ATTENUATION, 1 = INVERSE_DISTANCE,
  // 2 = LINEAR_DISTANCE, 3 = EXPONENTIAL
  SoLoud.instance.set3dSourceAttenuation(handle, 2, 1); // LINEAR_DISTANCE
  strBuf.writeln('Set attenuation to LINEAR_DISTANCE');

  // Test doppler factor
  SoLoud.instance.set3dSourceDopplerFactor(handle, 1);
  strBuf.writeln('Set doppler factor to 1');

  // Test set3dSourceParameters (positional args)
  SoLoud.instance.set3dSourceParameters(
    handle,
    8, 0, 0, // position
    5, 0, 0, // velocity
  );
  strBuf.writeln('Set source parameters with combined call');

  // Test listener position changes (positional args)
  SoLoud.instance.set3dListenerPosition(2, 0, 0);
  strBuf.writeln('Moved listener to (2, 0, 0)');

  // Test listener orientation changes (positional args)
  SoLoud.instance.set3dListenerAt(1, 0, 0); // Looking at +X
  SoLoud.instance.set3dListenerUp(0, 1, 0);
  strBuf.writeln('Changed listener orientation');

  // Test listener velocity (positional args)
  SoLoud.instance.set3dListenerVelocity(5, 0, 0);
  strBuf.writeln('Set listener velocity');

  await delay(500);

  // Cleanup
  await SoLoud.instance.stop(handle);
  deinit();

  strBuf.writeln('3D audio tests completed successfully');
  return strBuf;
}

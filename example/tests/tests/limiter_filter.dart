import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test limiter filter as a single (per-sound) filter.
Future<OutputBuffer> testLimiterFilterSingle() async {
  final strBuf = OutputBuffer();

  if (kIsWeb || kIsWasm) {
    return strBuf
      ..write('WARNING: Web does not support single sound filters.')
      ..writeln();
  }

  await initialize();

  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/explosion.mp3',
  );

  // Get the limiter filter for this sound
  final filter = sound.filters.limiterFilter

    // Activate the filter on the sound
    ..activate();
  assert(filter.isActive, 'Limiter filter should be active');
  strBuf.writeln('Limiter single filter activated');

  // Play the sound and get the handle
  final handle = SoLoud.instance.play(sound);
  strBuf.writeln('Playing sound with handle: $handle');

  // Set output ceiling (in dB, typically negative) using the handle
  filter.outputCeiling(soundHandle: handle).value = -3;
  strBuf.writeln('Set output ceiling to -3.0 dB');

  assert(
    closeTo(filter.outputCeiling(soundHandle: handle).value, -3, 0.1),
    'Output ceiling should be -3.0',
  );

  // Test multiple sounds with limiting
  final sound2 = await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  SoLoud.instance.play(sound);
  SoLoud.instance.play(sound2);
  strBuf.writeln('Playing multiple sounds with limiter active');

  await delay(1000);

  filter.outputCeiling(soundHandle: handle).value = -6;
  strBuf.writeln('Changed output ceiling to -6.0 dB (more limiting)');

  await delay(800);

  filter.outputCeiling(soundHandle: handle).fadeFilterParameter(
        to: -1,
        time: const Duration(milliseconds: 500),
      );
  strBuf.writeln('Fading output ceiling to -1.0 dB');

  await delay(600);

  filter.deactivate();
  assert(!filter.isActive, 'Limiter filter should be inactive');
  strBuf.writeln('Limiter single filter deactivated');

  await SoLoud.instance.disposeSource(sound2);

  deinit();

  strBuf.writeln('Limiter single filter tests completed successfully');
  return strBuf;
}

/// Test limiter filter as a global filter.
Future<OutputBuffer> testLimiterFilterGlobal() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Get the global limiter filter
  final filter = SoLoud.instance.filters.limiterFilter

    // Activate the global filter
    ..activate();
  assert(filter.isActive, 'Global limiter filter should be active');
  strBuf.writeln('Limiter global filter activated');

  // Set output ceiling (no handle needed for global)
  filter.outputCeiling.value = -3;
  strBuf.writeln('Set output ceiling to -3.0 dB');

  assert(
    closeTo(filter.outputCeiling.value, -3, 0.1),
    'Output ceiling should be -3.0',
  );

  // Load and play multiple sounds to test limiting
  final sound1 = await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final sound2 = await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');

  SoLoud.instance.play(sound1);
  SoLoud.instance.play(sound1);
  SoLoud.instance.play(sound2);
  strBuf.writeln('Playing multiple sounds with global limiter active');

  await delay(1000);

  filter.outputCeiling.value = -6;
  strBuf.writeln('Changed output ceiling to -6.0 dB (more limiting)');

  await delay(800);

  filter.outputCeiling.fadeFilterParameter(
    to: -1,
    time: const Duration(milliseconds: 500),
  );
  strBuf.writeln('Fading output ceiling to -1.0 dB');

  await delay(600);

  filter.deactivate();
  assert(!filter.isActive, 'Global limiter filter should be inactive');
  strBuf.writeln('Limiter global filter deactivated');

  await SoLoud.instance.disposeSource(sound1);
  await SoLoud.instance.disposeSource(sound2);

  deinit();

  strBuf.writeln('Limiter global filter tests completed successfully');
  return strBuf;
}

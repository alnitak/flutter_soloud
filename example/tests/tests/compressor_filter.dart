import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test compressor filter as a single (per-sound) filter.
Future<OutputBuffer> testCompressorFilterSingle() async {
  final strBuf = OutputBuffer();

  if (kIsWeb || kIsWasm) {
    return strBuf
      ..write('WARNING: Web does not support single sound filters.')
      ..writeln();
  }

  await initialize();

  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
  );

  // Get the compressor filter for this sound
  final filter = sound.filters.compressorFilter

    // Activate the filter on the sound
    ..activate();
  assert(filter.isActive, 'Compressor filter should be active');
  strBuf.writeln('Compressor single filter activated');

  // Play the sound and get the handle
  final handle = SoLoud.instance.play(sound, volume: 0.8);
  strBuf.writeln('Playing sound with handle: $handle');

  // Set compressor parameters using the handle
  filter.wet(soundHandle: handle).value = 0.8;
  strBuf.writeln('Set wet parameter to 0.8');

  assert(
    closeTo(filter.wet(soundHandle: handle).value, 0.8, 0.01),
    'Wet parameter should be 0.8',
  );

  filter.threshold(soundHandle: handle).value = -12;
  filter.ratio(soundHandle: handle).value = 4;
  strBuf.writeln('Set threshold to -12 and ratio to 4:1');

  await delay(1000);

  filter.wet(soundHandle: handle).value = 0.5;
  strBuf.writeln('Adjusted wet to 0.5 (less compression)');

  await delay(800);

  filter.wet(soundHandle: handle).value = 1;
  strBuf.writeln('Set wet to 1.0 (full compression)');

  await delay(800);

  filter.wet(soundHandle: handle).fadeFilterParameter(
        to: 0.3,
        time: const Duration(milliseconds: 500),
      );
  strBuf.writeln('Fading compression to 0.3');

  await delay(600);

  filter.deactivate();
  assert(!filter.isActive, 'Compressor filter should be inactive');
  strBuf.writeln('Compressor single filter deactivated');

  deinit();

  strBuf.writeln('Compressor single filter tests completed successfully');
  return strBuf;
}

/// Test compressor filter as a global filter.
Future<OutputBuffer> testCompressorFilterGlobal() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Get the global compressor filter
  final filter = SoLoud.instance.filters.compressorFilter

    // Activate the global filter
    ..activate();
  assert(filter.isActive, 'Global compressor filter should be active');
  strBuf.writeln('Compressor global filter activated');

  // Set compressor parameters (no handle needed for global)
  filter.wet.value = 0.8;
  strBuf.writeln('Set wet parameter to 0.8');

  assert(
    closeTo(filter.wet.value, 0.8, 0.01),
    'Wet parameter should be 0.8',
  );

  filter.threshold.value = -12;
  filter.ratio.value = 4;
  strBuf.writeln('Set threshold to -12 and ratio to 4:1');

  // Load and play a sound
  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
  );
  SoLoud.instance.play(sound, volume: 0.8);
  strBuf.writeln('Playing sound with global compressor active');

  await delay(1000);

  filter.wet.value = 0.5;
  strBuf.writeln('Adjusted wet to 0.5 (less compression)');

  await delay(800);

  filter.wet.value = 1;
  strBuf.writeln('Set wet to 1.0 (full compression)');

  await delay(800);

  filter.wet.fadeFilterParameter(
    to: 0.3,
    time: const Duration(milliseconds: 500),
  );
  strBuf.writeln('Fading compression to 0.3');

  await delay(600);

  filter.deactivate();
  assert(!filter.isActive, 'Global compressor filter should be inactive');
  strBuf.writeln('Compressor global filter deactivated');

  deinit();

  strBuf.writeln('Compressor global filter tests completed successfully');
  return strBuf;
}

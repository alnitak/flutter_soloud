import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test parametric equalizer filter as a single (per-sound) filter.
Future<OutputBuffer> testEqualizerFilterSingle() async {
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

  // Get the parametric EQ filter for this sound
  final filter = sound.filters.parametricEqFilter

    // Activate the filter on the sound
    ..activate();
  assert(filter.isActive, 'Parametric EQ filter should be active');
  strBuf.writeln('Parametric EQ single filter activated');

  // Play the sound and get the handle
  final handle = SoLoud.instance.play(sound);
  strBuf.writeln('Playing sound with handle: $handle');

  // Set number of bands and window size (these don't need handle)
  filter.numBands(soundHandle: handle).value = 8;
  filter.stftWindowSize(soundHandle: handle).value = 2048;
  strBuf.writeln('Set numBands to 8 and stftWindowSize to 2048');

  // Set band gains using bandGain(index) with the handle
  filter.bandGain(0, soundHandle: handle).value = 0.5;
  filter.bandGain(1, soundHandle: handle).value = 0.8;
  filter.bandGain(2, soundHandle: handle).value = 1.0;
  filter.bandGain(3, soundHandle: handle).value = 1.2;
  filter.bandGain(4, soundHandle: handle).value = 0.9;
  strBuf.writeln('Set 5 band gains with varying values');

  // Verify some band gains
  assert(
    closeTo(filter.bandGain(0, soundHandle: handle).value, 0.5, 0.01),
    'Band 0 gain should be 0.5',
  );
  assert(
    closeTo(filter.bandGain(4, soundHandle: handle).value, 0.9, 0.01),
    'Band 4 gain should be 0.9',
  );

  await delay(1000);

  // Test fading a band gain
  filter.bandGain(0, soundHandle: handle).fadeFilterParameter(
        to: 2,
        time: const Duration(milliseconds: 500),
      );
  strBuf.writeln('Fading band 0 gain to 2.0');

  await delay(600);

  // Test oscillating a band gain
  filter.bandGain(1, soundHandle: handle).oscillateFilterParameter(
        from: 0.5,
        to: 1.5,
        time: const Duration(milliseconds: 400),
      );
  strBuf.writeln('Oscillating band 1 gain');

  await delay(500);

  // Deactivate the filter
  filter.deactivate();
  assert(!filter.isActive, 'Parametric EQ filter should be inactive');
  strBuf.writeln('Parametric EQ single filter deactivated');

  await delay(500);

  deinit();

  strBuf.writeln('Parametric EQ single filter tests completed successfully');
  return strBuf;
}

/// Test parametric equalizer filter as a global filter.
Future<OutputBuffer> testEqualizerFilterGlobal() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Get the global parametric EQ filter
  final filter = SoLoud.instance.filters.parametricEqFilter

    // Activate the global filter
    ..activate();
  assert(filter.isActive, 'Global parametric EQ filter should be active');
  strBuf.writeln('Parametric EQ global filter activated');

  // Set number of bands and window size (no handle needed for global)
  filter.numBands.value = 8;
  filter.stftWindowSize.value = 2048;
  strBuf.writeln('Set numBands to 8 and stftWindowSize to 2048');

  // Load and play a sound
  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
  );
  SoLoud.instance.play(sound);
  strBuf.writeln('Playing sound with global EQ active');

  // Set band gains (no handle needed for global filters)
  filter.bandGain(0).value = 0.5;
  filter.bandGain(1).value = 0.8;
  filter.bandGain(2).value = 1.0;
  filter.bandGain(3).value = 1.2;
  filter.bandGain(4).value = 0.9;
  strBuf.writeln('Set 5 band gains with varying values');

  // Verify some band gains
  assert(
    closeTo(filter.bandGain(0).value, 0.5, 0.01),
    'Band 0 gain should be 0.5',
  );
  assert(
    closeTo(filter.bandGain(4).value, 0.9, 0.01),
    'Band 4 gain should be 0.9',
  );

  await delay(1000);

  // Test fading a band gain
  filter.bandGain(0).fadeFilterParameter(
        to: 2,
        time: const Duration(milliseconds: 500),
      );
  strBuf.writeln('Fading band 0 gain to 2.0');

  await delay(600);

  // Test oscillating a band gain
  filter.bandGain(1).oscillateFilterParameter(
        from: 0.5,
        to: 1.5,
        time: const Duration(milliseconds: 400),
      );
  strBuf.writeln('Oscillating band 1 gain');

  await delay(500);

  // Deactivate the filter
  filter.deactivate();
  assert(!filter.isActive, 'Global parametric EQ filter should be inactive');
  strBuf.writeln('Parametric EQ global filter deactivated');

  await delay(500);

  deinit();

  strBuf.writeln('Parametric EQ global filter tests completed successfully');
  return strBuf;
}

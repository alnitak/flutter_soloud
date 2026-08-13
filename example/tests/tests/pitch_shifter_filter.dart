import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test pitch shifter filter as a single (per-sound) filter.
Future<OutputBuffer> testPitchShifterFilterSingle() async {
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

  // Get the pitch shift filter for this sound
  final filter = sound.filters.pitchShiftFilter

    // Activate the filter on the sound
    ..activate();
  assert(filter.isActive, 'Pitch shift filter should be active');
  strBuf.writeln('Pitch shift single filter activated');

  // Play the sound and get the handle
  final handle = SoLoud.instance.play(sound);
  strBuf.writeln('Playing sound with handle: $handle');

  // Set shift parameter using the handle
  filter.shift(soundHandle: handle).value = 1.5;
  strBuf.writeln('Set pitch shift to 1.5 (higher pitch)');

  assert(
    closeTo(filter.shift(soundHandle: handle).value, 1.5, 0.01),
    'Pitch shift should be 1.5',
  );

  await delay(1000);

  filter.shift(soundHandle: handle).value = 0.75;
  strBuf.writeln('Changed pitch shift to 0.75 (lower pitch)');

  await delay(500);

  filter.shift(soundHandle: handle).fadeFilterParameter(
        to: 2,
        time: const Duration(milliseconds: 500),
      );
  strBuf.writeln('Fading pitch shift to 2.0');

  await delay(600);

  filter.semitones(soundHandle: handle).value = 7;
  strBuf.writeln('Set semitones shift to 7');

  await delay(800);

  filter.deactivate();
  assert(!filter.isActive, 'Pitch shift filter should be inactive');
  strBuf.writeln('Pitch shift single filter deactivated');

  deinit();

  strBuf.writeln('Pitch shift single filter tests completed successfully');
  return strBuf;
}

/// Test pitch shifter filter as a global filter.
Future<OutputBuffer> testPitchShifterFilterGlobal() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Get the global pitch shift filter
  final filter = SoLoud.instance.filters.pitchShiftFilter

    // Activate the global filter
    ..activate();
  assert(filter.isActive, 'Global pitch shift filter should be active');
  strBuf.writeln('Pitch shift global filter activated');

  // Load and play a sound
  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/explosion.mp3',
  );
  SoLoud.instance.play(sound);
  strBuf.writeln('Playing sound with global pitch shift active');

  // Set shift parameter (no handle needed for global)
  filter.shift.value = 1.5;
  strBuf.writeln('Set pitch shift to 1.5 (higher pitch)');

  assert(
    closeTo(filter.shift.value, 1.5, 0.01),
    'Pitch shift should be 1.5',
  );

  await delay(1000);

  filter.shift.value = 0.75;
  strBuf.writeln('Changed pitch shift to 0.75 (lower pitch)');

  await delay(500);

  filter.shift.fadeFilterParameter(
    to: 2,
    time: const Duration(milliseconds: 500),
  );
  strBuf.writeln('Fading pitch shift to 2.0');

  await delay(600);

  filter.semitones.value = 7;
  strBuf.writeln('Set semitones shift to 7');

  await delay(800);

  filter.deactivate();
  assert(!filter.isActive, 'Global pitch shift filter should be inactive');
  strBuf.writeln('Pitch shift global filter deactivated');

  deinit();

  strBuf.writeln('Pitch shift global filter tests completed successfully');
  return strBuf;
}

// ignore_for_file: prefer_int_literals

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test sound filters.
Future<OutputBuffer> testSoundFilters() async {
  final strBuf = OutputBuffer();

  if (kIsWeb || kIsWasm) {
    return strBuf
      ..write('WARNING: Web does not support single sound filters.')
      ..writeln();
  }

  await initialize();

  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
    // mode: LoadMode.disk,
  );

  final filter = sound.filters.echoFilter;

  /// Add filter to the sound.
  // ignore: cascade_invocations
  filter.activate();

  /// Set a handle filter. It must be set before it starts playing.
  final h1 = SoLoud.instance.play(sound);

  /// Check if filter is active.
  assert(
    filter.isActive,
    'The filter has not been activate!',
  );

  /// Use the `Wet` attribute index.
  const value = 0.2;
  filter.wet(soundHandle: h1).value = value;
  final g = filter.wet(soundHandle: h1).value;
  assert(
    closeTo(g, value, 0.001),
    'Setting attribute to $value but obtained $g',
  );

  /// Oscillate wet parameter.
  filter.wet(soundHandle: h1).oscillateFilterParameter(
        from: 0.01,
        to: 2,
        time: const Duration(seconds: 2),
      );

  await delay(2000);

  /// Test fading filter parameter
  strBuf.writeln('Fading wet parameter to 0.5');
  filter.wet(soundHandle: h1).fadeFilterParameter(
        to: 0.5,
        time: const Duration(milliseconds: 500),
      );

  await delay(600);

  /// Test oscillating filter parameter
  strBuf.writeln('Oscillating wet parameter');
  filter.wet(soundHandle: h1).oscillateFilterParameter(
        from: 0.1,
        to: 1.0,
        time: const Duration(milliseconds: 800),
      );

  await delay(2000);

  /// Remove the filter.
  try {
    filter.deactivate();
  } on Exception catch (e) {
    strBuf
      ..write(e)
      ..writeln();
  }

  /// Test filter reactivation
  strBuf.writeln('Reactivating filter');
  filter.activate();
  assert(filter.isActive, 'Filter should be active after reactivation');
  filter.deactivate();

  SoLoud.instance.play(sound);

  /// Check if filter has been deactivated.
  assert(
    !filter.isActive,
    'The filter is still active after removing it!',
  );

  deinit();
  return strBuf;
}

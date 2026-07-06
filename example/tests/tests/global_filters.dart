import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test global filters.
Future<OutputBuffer> testGlobalFilters() async {
  final strBuf = OutputBuffer();
  await initialize();

  late final AudioSource sound;
  try {
    sound = await SoLoud.instance.loadAsset(
      'assets/audio/8_bit_mentality.mp3',
      mode: LoadMode.disk,
    );
  } on Exception catch (e) {
    return strBuf
      ..write(e)
      ..writeln();
  }

  final filter = SoLoud.instance.filters.echoFilter;

  /// Add filter to the sound.
  // ignore: cascade_invocations
  filter.activate();

  SoLoud.instance.play(sound);

  /// Check if filter is active.
  assert(
    filter.isActive,
    'The filter has not been activate!',
  );

  /// Use the `Wet` attribute index.
  const value = 0.2;
  filter.wet.value = value;
  final g = filter.wet.value;
  assert(
    closeTo(g, value, 0.001),
    'Setting attribute to $value but optained $g',
  );

  /// Oscillate wet parameter.
  filter.wet.oscillateFilterParameter(
    from: 0.01,
    to: 2,
    time: const Duration(seconds: 2),
  );

  await delay(2000);

  /// Test fading filter parameter
  strBuf.writeln('Fading global filter wet parameter to 0.5');
  filter.wet.fadeFilterParameter(
    to: 0.5,
    time: const Duration(milliseconds: 500),
  );

  await delay(600);

  /// Test oscillating filter parameter
  strBuf.writeln('Oscillating global filter wet parameter');
  filter.wet.oscillateFilterParameter(
    from: 0.1,
    to: 0.8,
    time: const Duration(milliseconds: 800),
  );

  await delay(2000);

  /// Remove the filter
  try {
    filter.deactivate();
  } on Exception catch (e) {
    strBuf
      ..write(e)
      ..writeln();
  }

  /// Check if filter has been deactivated.
  assert(
    !filter.isActive,
    'The filter has not been activate!',
  );

  deinit();
  return strBuf;
}

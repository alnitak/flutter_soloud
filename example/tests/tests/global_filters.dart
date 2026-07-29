import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/filters/filters.dart';

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

/// Test setting and getting back all the parameters of all the available
/// global filters.
///
Future<OutputBuffer> testSetGetGlobalFilters() async {
  final strBuf = OutputBuffer();
  await initialize();

  final filters = SoLoud.instance.filters;

  /// All the filters with a valid (in range and not default) value for
  /// each of their parameters.
  final tests = <(FilterBase, List<(String, FilterParam, double)>)>[
    (
      filters.biquadResonantFilter,
      [
        ('wet', filters.biquadResonantFilter.wet, 0.5),
        ('type', filters.biquadResonantFilter.type, 1),
        ('frequency', filters.biquadResonantFilter.frequency, 2135),
        ('resonance', filters.biquadResonantFilter.resonance, 2),
      ],
    ),
    (
      filters.echoFilter,
      [
        ('wet', filters.echoFilter.wet, 0.5),
        ('delay', filters.echoFilter.delay, 0.2),
        ('decay', filters.echoFilter.decay, 0.5),
        ('filter', filters.echoFilter.filter, 0.5),
      ],
    ),
    (
      filters.lofiFilter,
      [
        ('wet', filters.lofiFilter.wet, 0.5),
        ('samplerate', filters.lofiFilter.samplerate, 8000),
        ('bitdepth', filters.lofiFilter.bitdepth, 4),
      ],
    ),
    (
      filters.flangerFilter,
      [
        ('wet', filters.flangerFilter.wet, 0.5),
        ('delay', filters.flangerFilter.delay, 0.1),
        ('freq', filters.flangerFilter.freq, 10),
      ],
    ),
    (
      filters.bassBoostFilter,
      [
        ('wet', filters.bassBoostFilter.wet, 0.5),
        ('boost', filters.bassBoostFilter.boost, 2),
      ],
    ),
    (
      filters.waveShaperFilter,
      [
        ('wet', filters.waveShaperFilter.wet, 0.5),
        ('amount', filters.waveShaperFilter.amount, 0.5),
      ],
    ),
    (
      filters.robotizeFilter,
      [
        ('wet', filters.robotizeFilter.wet, 0.5),
        ('frequency', filters.robotizeFilter.frequency, 30),
        ('waveform', filters.robotizeFilter.waveform, 2),
      ],
    ),
    (
      filters.freeverbFilter,
      [
        ('wet', filters.freeverbFilter.wet, 0.5),
        ('freeze', filters.freeverbFilter.freeze, 0.5),
        ('roomSize', filters.freeverbFilter.roomSize, 0.8),
        ('damp', filters.freeverbFilter.damp, 0.5),
        ('width', filters.freeverbFilter.width, 0.5),
      ],
    ),
    (
      filters.pitchShiftFilter,
      [
        ('wet', filters.pitchShiftFilter.wet, 0.5),
        ('shift', filters.pitchShiftFilter.shift, 1.5),
        ('semitones', filters.pitchShiftFilter.semitones, -3),
      ],
    ),
    (
      filters.limiterFilter,
      [
        ('wet', filters.limiterFilter.wet, 0.5),
        ('threshold', filters.limiterFilter.threshold, -10),
        ('outputCeiling', filters.limiterFilter.outputCeiling, -1),
        ('kneeWidth', filters.limiterFilter.kneeWidth, 5),
        ('releaseTime', filters.limiterFilter.releaseTime, 100),
        ('attackTime', filters.limiterFilter.attackTime, 10),
      ],
    ),
    (
      filters.compressorFilter,
      [
        ('wet', filters.compressorFilter.wet, 0.5),
        ('threshold', filters.compressorFilter.threshold, -20),
        ('makeupGain', filters.compressorFilter.makeupGain, 5),
        ('kneeWidth', filters.compressorFilter.kneeWidth, 10),
        ('ratio', filters.compressorFilter.ratio, 4),
        ('attackTime', filters.compressorFilter.attackTime, 20),
        ('releaseTime', filters.compressorFilter.releaseTime, 200),
      ],
    ),
  ];

  for (final (filter, params) in tests) {
    filter.activate();
    assert(filter.isActive, '${filter.filterType} has not been activated!');
    strBuf.writeln('${filter.filterType}:');
    for (final (name, param, value) in params) {
      param.value = value;
      final read = param.value;
      strBuf.writeln('  $name set:$value read:$read');
      assert(
        closeTo(read, value, 0.01),
        '${filter.filterType}.$name set:$value but read:$read',
      );
    }
    filter.deactivate();
    assert(!filter.isActive, '${filter.filterType} has not been deactivated!');
  }

  /// The parametric EQ is tested separately because its band gain
  /// parameters depend on the `numBands` parameter.
  final eq = filters.parametricEqFilter..activate();
  assert(eq.isActive, 'Parametric EQ has not been activated!');
  strBuf.writeln('${eq.filterType}:');

  final eqParams = <(String, FilterParam, double)>[
    ('wet', eq.wet, 0.5),
    ('stftWindowSize', eq.stftWindowSize, 512),
    ('numBands', eq.numBands, 8),
    for (var i = 0; i < 8; i++)
      ('bandGain($i)', eq.bandGain(i), 0.5 + i * 0.25),
  ];
  for (final (name, param, value) in eqParams) {
    param.value = value;
    final read = param.value;
    strBuf.writeln('  $name set:$value read:$read');
    assert(
      closeTo(read, value, 0.01),
      '${eq.filterType}.$name set:$value but read:$read',
    );
  }
  eq.deactivate();
  assert(!eq.isActive, 'Parametric EQ has not been deactivated!');

  deinit();

  strBuf.writeln('All global filters set/get tests passed');
  return strBuf;
}

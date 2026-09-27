import 'dart:io';

import 'package:flutter_soloud/src/bindings/flutter_soloud_ffigen.dart'
    as native;
import 'package:flutter_soloud/src/filters/amplitude_modulator_filter.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:test/test.dart';

/// Parses the `FilterType` enumerators of `src/enums.h` in declaration order.
/// They have no explicit values, so each value is its position.
List<String> _nativeFilterTypes() {
  final source = File('src/enums.h').readAsStringSync();
  final body = RegExp(
    r'typedef enum FilterType \{(.*?)\} FilterType_t;',
    dotAll: true,
  ).firstMatch(source);
  expect(
    body,
    isNotNull,
    reason: 'Could not find the FilterType enum in src/enums.h',
  );
  return RegExp(r'^\s*(\w+)\s*,?\s*$', multiLine: true)
      .allMatches(body!.group(1)!)
      .map((m) => m.group(1)!)
      .toList();
}

void main() {
  test('FilterType is in sync with src/enums.h and the ffigen bindings', () {
    final nativeNames = _nativeFilterTypes();
    expect(
      nativeNames.length,
      FilterType.values.length,
      reason:
          'The C++ `FilterType` enum has ${nativeNames.length} values while '
          'the Dart one has ${FilterType.values.length}. Filter types are '
          'passed over the ABI (and to WASM) as `FilterType.index`.',
    );
    expect(
      native.FilterType.values.map((e) => e.name).toList(),
      nativeNames,
      reason: 'lib/src/bindings/flutter_soloud_ffigen.dart is stale: '
          'run `dart run ffigen --config ffigen.yaml`.',
    );
    for (final e in native.FilterType.values) {
      expect(e.value, nativeNames.indexOf(e.name));
    }
  });

  test('existing filter type values are not renumbered', () {
    const expected = {
      FilterType.biquadResonantFilter: 0,
      FilterType.echoFilter: 1,
      FilterType.lofiFilter: 2,
      FilterType.flangerFilter: 3,
      FilterType.bassboostFilter: 4,
      FilterType.waveShaperFilter: 5,
      FilterType.robotizeFilter: 6,
      FilterType.freeverbFilter: 7,
      FilterType.pitchShiftFilter: 8,
      FilterType.limiterFilter: 9,
      FilterType.compressorFilter: 10,
      FilterType.parametricEq: 11,
      FilterType.amplitudeModulatorFilter: 12,
    };
    for (final MapEntry(key: type, value: index) in expected.entries) {
      expect(type.index, index, reason: '$type moved');
    }
  });

  test('amplitude modulator maps to the native AmplitudeModulatorFilter', () {
    const type = FilterType.amplitudeModulatorFilter;
    expect(
      native.FilterType.fromValue(type.index),
      native.FilterType.AmplitudeModulatorFilter,
    );
    expect(type.numParameters, 2);
    expect(type.toString(), 'Amplitude Modulator');
  });

  test('amplitude modulator parameters', () {
    expect(AmplitudeModulatorEnum.values.length, 2);

    expect(AmplitudeModulatorEnum.wet.index, 0);
    expect(AmplitudeModulatorEnum.wet.toString(), 'Wet');
    expect(AmplitudeModulatorEnum.wet.min, 0);
    expect(AmplitudeModulatorEnum.wet.max, 1);
    expect(AmplitudeModulatorEnum.wet.def, 1);

    expect(AmplitudeModulatorEnum.frequency.index, 1);
    expect(AmplitudeModulatorEnum.frequency.toString(), 'Frequency');
    expect(AmplitudeModulatorEnum.frequency.min, 0.1);
    expect(AmplitudeModulatorEnum.frequency.max, 20000);
    expect(AmplitudeModulatorEnum.frequency.def, 440);

    const global = AmplitudeModulatorGlobal();
    expect(global.filterType, FilterType.amplitudeModulatorFilter);
    expect(global.queryWet, AmplitudeModulatorEnum.wet);
    expect(global.queryFrequency, AmplitudeModulatorEnum.frequency);
  });

  test('amplitude modulator ranges match the native filter', () {
    final source = File(
      'src/filters/amplitude_modulator_filter.cpp',
    ).readAsStringSync();
    double nativeBound(String fn, String attr) {
      final body = RegExp(
        'float AmplitudeModulator::$fn\\(.*?\\n\\}',
        dotAll: true,
      ).firstMatch(source)!.group(0)!;
      final m = RegExp(
        'case $attr:\\s*return ([0-9.]+)f;',
      ).firstMatch(body)!;
      return double.parse(m.group(1)!);
    }

    expect(nativeBound('getParamMin', 'WET'), AmplitudeModulatorEnum.wet.min);
    expect(nativeBound('getParamMax', 'WET'), AmplitudeModulatorEnum.wet.max);
    expect(
      nativeBound('getParamMin', 'FREQUENCY'),
      AmplitudeModulatorEnum.frequency.min,
    );
    expect(
      nativeBound('getParamMax', 'FREQUENCY'),
      AmplitudeModulatorEnum.frequency.max,
    );
  });
}

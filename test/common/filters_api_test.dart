// The Dart filter API (lib/src/filters/) with the real engine. For every
// built-in filter, globally and on a single sound: activate and deactivate,
// read the defaults, set and read back every parameter, fade and oscillate a
// parameter, and the errors for filters that are not active, handles that
// are not voices, sounds that are gone and attribute indices that do not
// exist. Then the parametric EQ and custom filter specifics.
@Tags(['device'])
library;

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/filters/amplitude_modulator_filter.dart';
import 'package:flutter_soloud/src/filters/bassboost_filter.dart';
import 'package:flutter_soloud/src/filters/biquad_resonant_filter.dart';
import 'package:flutter_soloud/src/filters/compressor.dart';
import 'package:flutter_soloud/src/filters/echo_filter.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_soloud/src/filters/flanger_filter.dart';
import 'package:flutter_soloud/src/filters/freeverb_filter.dart';
import 'package:flutter_soloud/src/filters/limiter.dart';
import 'package:flutter_soloud/src/filters/lofi_filter.dart';
import 'package:flutter_soloud/src/filters/parametric_eq.dart';
import 'package:flutter_soloud/src/filters/pitchshift_filter.dart';
import 'package:flutter_soloud/src/filters/robotize_filter.dart';
import 'package:flutter_soloud/src/filters/wave_shaper_filter.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// One parameter of a filter: its range and default, and how to reach it
/// globally and on a voice of a sound.
class _Param {
  const _Param(this.name, this.min, this.max, this.def, this.global, this.on);

  final String name;
  final double min;
  final double max;
  final double def;
  final FilterParam Function() global;
  final FilterParam Function(FiltersSingle filters, SoundHandle handle) on;

  /// A value in range that differs from the default.
  double get other {
    var v = (min + max) / 2;
    final integral = min == min.roundToDouble() && max == max.roundToDouble();
    if (integral && max - min >= 2) v = v.roundToDouble();
    if ((v - def).abs() < 1e-6) v = min + (max - min) / 4;
    return v;
  }
}

/// A built-in filter as seen from the Dart API.
class _Filter {
  const _Filter(this.type, this.global, this.single, this.params);

  final FilterType type;
  final FilterBase global;
  final FilterBase Function(FiltersSingle filters) single;
  final List<_Param> params;
}

_Param _p(
  String name,
  double min,
  double max,
  double def,
  FilterParam Function() global,
  FilterParam Function(FiltersSingle f, SoundHandle h) on,
) => _Param(name, min, max, def, global, on);

final _globals = SoLoud.instance.filters;

final List<_Filter> _filters = [
  _Filter(
    FilterType.biquadResonantFilter,
    _globals.biquadResonantFilter,
    (f) => f.biquadResonantFilter,
    [
      for (final e in BiquadResonantEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            BiquadResonantEnum.wet => _globals.biquadResonantFilter.wet,
            BiquadResonantEnum.type => _globals.biquadResonantFilter.type,
            BiquadResonantEnum.frequency =>
              _globals.biquadResonantFilter.frequency,
            BiquadResonantEnum.resonance =>
              _globals.biquadResonantFilter.resonance,
          },
          (f, h) => switch (e) {
            BiquadResonantEnum.wet => f.biquadResonantFilter.wet(
              soundHandle: h,
            ),
            BiquadResonantEnum.type => f.biquadResonantFilter.type(
              soundHandle: h,
            ),
            BiquadResonantEnum.frequency => f.biquadResonantFilter.frequency(
              soundHandle: h,
            ),
            BiquadResonantEnum.resonance => f.biquadResonantFilter.resonance(
              soundHandle: h,
            ),
          },
        ),
    ],
  ),
  _Filter(FilterType.echoFilter, _globals.echoFilter, (f) => f.echoFilter, [
    for (final e in EchoEnum.values)
      _p(
        e.name,
        e.min,
        e.max,
        e.def,
        () => switch (e) {
          EchoEnum.wet => _globals.echoFilter.wet,
          EchoEnum.delay => _globals.echoFilter.delay,
          EchoEnum.decay => _globals.echoFilter.decay,
          EchoEnum.filter => _globals.echoFilter.filter,
        },
        (f, h) => switch (e) {
          EchoEnum.wet => f.echoFilter.wet(soundHandle: h),
          EchoEnum.delay => f.echoFilter.delay(soundHandle: h),
          EchoEnum.decay => f.echoFilter.decay(soundHandle: h),
          EchoEnum.filter => f.echoFilter.filter(soundHandle: h),
        },
      ),
  ]),
  _Filter(FilterType.lofiFilter, _globals.lofiFilter, (f) => f.lofiFilter, [
    for (final e in LofiEnum.values)
      _p(
        e.name,
        e.min,
        e.max,
        e.def,
        () => switch (e) {
          LofiEnum.wet => _globals.lofiFilter.wet,
          LofiEnum.samplerate => _globals.lofiFilter.samplerate,
          LofiEnum.bitdepth => _globals.lofiFilter.bitdepth,
        },
        (f, h) => switch (e) {
          LofiEnum.wet => f.lofiFilter.wet(soundHandle: h),
          LofiEnum.samplerate => f.lofiFilter.samplerate(soundHandle: h),
          LofiEnum.bitdepth => f.lofiFilter.bitdepth(soundHandle: h),
        },
      ),
  ]),
  _Filter(
    FilterType.flangerFilter,
    _globals.flangerFilter,
    (f) => f.flangerFilter,
    [
      for (final e in FlangerEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            FlangerEnum.wet => _globals.flangerFilter.wet,
            FlangerEnum.delay => _globals.flangerFilter.delay,
            FlangerEnum.freq => _globals.flangerFilter.freq,
          },
          (f, h) => switch (e) {
            FlangerEnum.wet => f.flangerFilter.wet(soundHandle: h),
            FlangerEnum.delay => f.flangerFilter.delay(soundHandle: h),
            FlangerEnum.freq => f.flangerFilter.freq(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.bassboostFilter,
    _globals.bassBoostFilter,
    (f) => f.bassBoostFilter,
    [
      for (final e in BassBoostEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            BassBoostEnum.wet => _globals.bassBoostFilter.wet,
            BassBoostEnum.boost => _globals.bassBoostFilter.boost,
          },
          (f, h) => switch (e) {
            BassBoostEnum.wet => f.bassBoostFilter.wet(soundHandle: h),
            BassBoostEnum.boost => f.bassBoostFilter.boost(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.waveShaperFilter,
    _globals.waveShaperFilter,
    (f) => f.waveShaperFilter,
    [
      for (final e in WaveShaperEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            WaveShaperEnum.wet => _globals.waveShaperFilter.wet,
            WaveShaperEnum.amount => _globals.waveShaperFilter.amount,
          },
          (f, h) => switch (e) {
            WaveShaperEnum.wet => f.waveShaperFilter.wet(soundHandle: h),
            WaveShaperEnum.amount => f.waveShaperFilter.amount(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.robotizeFilter,
    _globals.robotizeFilter,
    (f) => f.robotizeFilter,
    [
      for (final e in RobotizeEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            RobotizeEnum.wet => _globals.robotizeFilter.wet,
            RobotizeEnum.frequency => _globals.robotizeFilter.frequency,
            RobotizeEnum.waveform => _globals.robotizeFilter.waveform,
          },
          (f, h) => switch (e) {
            RobotizeEnum.wet => f.robotizeFilter.wet(soundHandle: h),
            RobotizeEnum.frequency => f.robotizeFilter.frequency(
              soundHandle: h,
            ),
            RobotizeEnum.waveform => f.robotizeFilter.waveform(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.freeverbFilter,
    _globals.freeverbFilter,
    (f) => f.freeverbFilter,
    [
      for (final e in FreeverbEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            FreeverbEnum.wet => _globals.freeverbFilter.wet,
            FreeverbEnum.freeze => _globals.freeverbFilter.freeze,
            FreeverbEnum.roomSize => _globals.freeverbFilter.roomSize,
            FreeverbEnum.damp => _globals.freeverbFilter.damp,
            FreeverbEnum.width => _globals.freeverbFilter.width,
          },
          (f, h) => switch (e) {
            FreeverbEnum.wet => f.freeverbFilter.wet(soundHandle: h),
            FreeverbEnum.freeze => f.freeverbFilter.freeze(soundHandle: h),
            FreeverbEnum.roomSize => f.freeverbFilter.roomSize(soundHandle: h),
            FreeverbEnum.damp => f.freeverbFilter.damp(soundHandle: h),
            FreeverbEnum.width => f.freeverbFilter.width(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.pitchShiftFilter,
    _globals.pitchShiftFilter,
    (f) => f.pitchShiftFilter,
    [
      for (final e in PitchShiftEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            PitchShiftEnum.wet => _globals.pitchShiftFilter.wet,
            PitchShiftEnum.shift => _globals.pitchShiftFilter.shift,
            PitchShiftEnum.semitones => _globals.pitchShiftFilter.semitones,
          },
          (f, h) => switch (e) {
            PitchShiftEnum.wet => f.pitchShiftFilter.wet(soundHandle: h),
            PitchShiftEnum.shift => f.pitchShiftFilter.shift(soundHandle: h),
            PitchShiftEnum.semitones => f.pitchShiftFilter.semitones(
              soundHandle: h,
            ),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.limiterFilter,
    _globals.limiterFilter,
    (f) => f.limiterFilter,
    [
      for (final e in Limiter.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            Limiter.wet => _globals.limiterFilter.wet,
            Limiter.threshold => _globals.limiterFilter.threshold,
            Limiter.outputCeiling => _globals.limiterFilter.outputCeiling,
            Limiter.kneeWidth => _globals.limiterFilter.kneeWidth,
            Limiter.releaseTime => _globals.limiterFilter.releaseTime,
            Limiter.attackTime => _globals.limiterFilter.attackTime,
          },
          (f, h) => switch (e) {
            Limiter.wet => f.limiterFilter.wet(soundHandle: h),
            Limiter.threshold => f.limiterFilter.threshold(soundHandle: h),
            Limiter.outputCeiling => f.limiterFilter.outputCeiling(
              soundHandle: h,
            ),
            Limiter.kneeWidth => f.limiterFilter.kneeWidth(soundHandle: h),
            Limiter.releaseTime => f.limiterFilter.releaseTime(soundHandle: h),
            Limiter.attackTime => f.limiterFilter.attackTime(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.compressorFilter,
    _globals.compressorFilter,
    (f) => f.compressorFilter,
    [
      for (final e in Compressor.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            Compressor.wet => _globals.compressorFilter.wet,
            Compressor.threshold => _globals.compressorFilter.threshold,
            Compressor.makeupGain => _globals.compressorFilter.makeupGain,
            Compressor.kneeWidth => _globals.compressorFilter.kneeWidth,
            Compressor.ratio => _globals.compressorFilter.ratio,
            Compressor.attackTime => _globals.compressorFilter.attackTime,
            Compressor.releaseTime => _globals.compressorFilter.releaseTime,
          },
          (f, h) => switch (e) {
            Compressor.wet => f.compressorFilter.wet(soundHandle: h),
            Compressor.threshold => f.compressorFilter.threshold(
              soundHandle: h,
            ),
            Compressor.makeupGain => f.compressorFilter.makeupGain(
              soundHandle: h,
            ),
            Compressor.kneeWidth => f.compressorFilter.kneeWidth(
              soundHandle: h,
            ),
            Compressor.ratio => f.compressorFilter.ratio(soundHandle: h),
            Compressor.attackTime => f.compressorFilter.attackTime(
              soundHandle: h,
            ),
            Compressor.releaseTime => f.compressorFilter.releaseTime(
              soundHandle: h,
            ),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.amplitudeModulatorFilter,
    _globals.amplitudeModulatorFilter,
    (f) => f.amplitudeModulatorFilter,
    [
      for (final e in AmplitudeModulatorEnum.values)
        _p(
          e.name,
          e.min,
          e.max,
          e.def,
          () => switch (e) {
            AmplitudeModulatorEnum.wet => _globals.amplitudeModulatorFilter.wet,
            AmplitudeModulatorEnum.frequency =>
              _globals.amplitudeModulatorFilter.frequency,
            AmplitudeModulatorEnum.waveform =>
              _globals.amplitudeModulatorFilter.waveform,
          },
          (f, h) => switch (e) {
            AmplitudeModulatorEnum.wet => f.amplitudeModulatorFilter.wet(
              soundHandle: h,
            ),
            AmplitudeModulatorEnum.frequency =>
              f.amplitudeModulatorFilter.frequency(soundHandle: h),
            AmplitudeModulatorEnum.waveform =>
              f.amplitudeModulatorFilter.waveform(soundHandle: h),
          },
        ),
    ],
  ),
  _Filter(
    FilterType.parametricEq,
    _globals.parametricEqFilter,
    (f) => f.parametricEqFilter,
    [
      _p(
        'wet',
        0,
        1,
        1,
        () => _globals.parametricEqFilter.wet,
        (f, h) => f.parametricEqFilter.wet(soundHandle: h),
      ),
      _p(
        'numBands',
        1,
        64,
        3,
        () => _globals.parametricEqFilter.numBands,
        (f, h) => f.parametricEqFilter.numBands(soundHandle: h),
      ),
      _p(
        'bandGain(1)',
        0,
        4,
        1,
        () => _globals.parametricEqFilter.bandGain(1),
        (f, h) => f.parametricEqFilter.bandGain(1, soundHandle: h),
      ),
      _p(
        'bandQ(2)',
        0.1,
        20,
        // Derived from the default band count (3): about 0.33.
        ParametricEqParam.getDefault(ParametricEqParam.bandQ(2)),
        () => _globals.parametricEqFilter.bandQ(2),
        (f, h) => f.parametricEqFilter.bandQ(2, soundHandle: h),
      ),
    ],
  ),
];

void main() {
  // One engine for the whole file (hundreds of short tests); every test
  // starts with no sound and no global filter.
  prepareEngineHost();
  final soloud = SoLoud.instance;
  setUpAll(() => initTestEngine(globalVolume: 0));
  tearDownAll(deinitTestEngine);

  setUp(() async {
    // Faders advance with the mixer, which runs while something plays.
    soloud.play(await loadTestWaveform(), looping: true);
  });
  tearDown(() async {
    // Also deactivates every global filter.
    if (soloud.isInitialized) await soloud.disposeAllSources();
  });

  test('every built-in filter type is covered', () {
    expect(
      _filters.map((f) => f.type).toSet(),
      FilterType.values.where((t) => t != FilterType.customFilter).toSet(),
    );
  });

  for (final filter in _filters) {
    group('${filter.type}', () {
      test('has the parameters the type declares', () {
        if (filter.type != FilterType.parametricEq) {
          expect(filter.params.length, filter.type.numParameters);
        }
        expect(filter.params.first.name, 'wet');
      });

      group('global', () {
        final f = filter.global;

        test('activate, isActive, index and deactivate', () {
          expect(f.isActive, isFalse);
          expect(f.index, -1);
          f.activate();
          expect(f.isActive, isTrue);
          expect(f.index, 0);
          f.deactivate();
          expect(f.isActive, isFalse);
        });

        test('activating twice throws SoLoudFilterAlreadyAddedException', () {
          f.activate();
          expect(f.activate, throwsA(isA<SoLoudFilterAlreadyAddedException>()));
          expect(f.isActive, isTrue);
          f.deactivate();
        });

        test('deactivating an inactive filter throws', () {
          expect(f.deactivate, throwsA(isA<SoLoudFilterNotFoundException>()));
        });

        test('a parameter of an inactive filter cannot be read', () {
          expect(
            () => filter.params.first.global().value,
            throwsA(isA<SoLoudFilterNotFoundException>()),
          );
          // Nor set.
          expect(
            () => filter.params.first.global().value = 0.5,
            throwsA(isA<SoLoudFilterNotFoundException>()),
          );
        });

        test('parameters start at their Dart defaults', () {
          f.activate();
          for (final p in filter.params) {
            expect(p.global().value, closeTo(p.def, 1e-4), reason: p.name);
          }
          f.deactivate();
        });

        test('parameters round-trip', () {
          f.activate();
          for (final p in filter.params) {
            p.global().value = p.other;
            expect(p.global().value, closeTo(p.other, 1e-3), reason: p.name);
          }
          f.deactivate();
        });

        test('out-of-range values are ignored', () {
          f.activate();
          for (final p in filter.params) {
            final before = p.global().value;
            p.global().value = p.max + 1;
            expect(p.global().value, before, reason: p.name);
            p.global().value = p.min - 1;
            expect(p.global().value, before, reason: p.name);
          }
          f.deactivate();
        });

        test('re-activating resets the parameters', () {
          f.activate();
          final wet = filter.params.first;
          final initial = wet.global().value;
          wet.global().value = 0.25;
          f
            ..deactivate()
            ..activate();
          expect(wet.global().value, initial);
          f.deactivate();
        });

        test('fadeFilterParameter reaches the target', () async {
          f.activate();
          final wet = filter.params.first.global()
            ..fadeFilterParameter(
              to: 0.3,
              time: const Duration(milliseconds: 100),
            );
          expect(
            await waitUntil(() => (wet.value - 0.3).abs() < 1e-3),
            isTrue,
            reason: 'wet is ${wet.value}',
          );
          f.deactivate();
        });

        test('oscillateFilterParameter stays within its range', () async {
          f.activate();
          final wet = filter.params.first.global()
            ..oscillateFilterParameter(
              from: 0.2,
              to: 0.8,
              time: const Duration(milliseconds: 80),
            );
          await Future<void>.delayed(const Duration(milliseconds: 100));
          final seen = <double>{};
          for (var i = 0; i < 12; i++) {
            await Future<void>.delayed(const Duration(milliseconds: 10));
            final v = wet.value;
            expect(v, inInclusiveRange(0.2 - 1e-3, 0.8 + 1e-3));
            seen.add(v);
          }
          expect(seen.length, greaterThan(1), reason: 'it oscillates');
          f.deactivate();
        });

        test('SoLoud.fadeGlobalFilterParameter reaches the target', () async {
          f.activate();
          soloud.fadeGlobalFilterParameter(
            filter.type,
            0,
            0.4,
            const Duration(milliseconds: 50),
          );
          final wet = filter.params.first.global();
          expect(await waitUntil(() => (wet.value - 0.4).abs() < 1e-3), isTrue);
          f.deactivate();
        });

        test('SoLoud.oscillateGlobalFilterParameter oscillates', () async {
          f.activate();
          soloud.oscillateGlobalFilterParameter(
            filter.type,
            0,
            0.1,
            0.9,
            const Duration(milliseconds: 80),
          );
          await Future<void>.delayed(const Duration(milliseconds: 100));
          final wet = filter.params.first.global();
          final seen = <double>{};
          for (var i = 0; i < 12; i++) {
            await Future<void>.delayed(const Duration(milliseconds: 10));
            seen.add(wet.value);
          }
          expect(seen.length, greaterThan(1), reason: 'it oscillates');
          f.deactivate();
        });

        test('an attribute index beyond the parameters is refused', () {
          f.activate();
          final beyond = FilterParam(null, null, filter.type, 195, -1000, 1000);
          expect(
            () => beyond.value,
            throwsA(isA<SoLoudInvalidParameterException>()),
          );
          expect(
            () => beyond.value = 1,
            throwsA(isA<SoLoudInvalidParameterException>()),
          );
          expect(
            () => beyond.fadeFilterParameter(to: 1, time: Duration.zero),
            throwsA(isA<SoLoudInvalidParameterException>()),
          );
          f.deactivate();
        });
      });

      group(
        'single sound',
        skip: kIsWeb
            ? 'filters for single sounds are not supported on the web'
            : null,
        () {
          late AudioSource sound;
          late FilterBase f;
          setUp(() async {
            sound = await loadTestWaveform();
            f = filter.single(sound.filters);
          });

          test('activate and deactivate do not touch the global filter', () {
            expect(f.isActive, isFalse);
            f.activate();
            expect(f.isActive, isTrue);
            expect(filter.global.isActive, isFalse);
            f.deactivate();
            expect(f.isActive, isFalse);
          });

          test('errors for twice and inactive', () {
            f.activate();
            expect(
              f.activate,
              throwsA(isA<SoLoudFilterAlreadyAddedException>()),
            );
            f.deactivate();
            expect(f.deactivate, throwsA(isA<SoLoudFilterNotFoundException>()));
          });

          test('parameters start at their Dart defaults', () {
            f.activate();
            final h = soloud.play(sound);
            for (final p in filter.params) {
              expect(
                p.on(sound.filters, h).value,
                closeTo(p.def, 1e-4),
                reason: p.name,
              );
            }
          });

          test('parameters round-trip', () {
            f.activate();
            final h = soloud.play(sound);
            for (final p in filter.params) {
              p.on(sound.filters, h).value = p.other;
              expect(
                p.on(sound.filters, h).value,
                closeTo(p.other, 1e-3),
                reason: p.name,
              );
            }
          });

          test('each voice has its own parameters', () {
            f.activate();
            final a = soloud.play(sound);
            final b = soloud.play(sound);
            final wet = filter.params.first;
            final initial = wet.on(sound.filters, b).value;
            wet.on(sound.filters, a).value = 0.25;
            expect(wet.on(sound.filters, a).value, closeTo(0.25, 1e-4));
            expect(wet.on(sound.filters, b).value, initial);
          });

          test('a voice playing when the filter is activated gets it', () {
            final h = soloud.play(sound);
            f.activate();
            expect(filter.params.first.on(sound.filters, h).value, 1);
          });

          test('fade and oscillate a voice parameter', () async {
            f.activate();
            final h = soloud.play(sound);
            final wet = filter.params.first.on(sound.filters, h)
              ..fadeFilterParameter(
                to: 0.3,
                time: const Duration(milliseconds: 100),
              );
            expect(
              await waitUntil(() => (wet.value - 0.3).abs() < 1e-3),
              isTrue,
            );
            wet.oscillateFilterParameter(
              from: 0.2,
              to: 0.8,
              time: const Duration(milliseconds: 80),
            );
            await Future<void>.delayed(const Duration(milliseconds: 100));
            final seen = <double>{};
            for (var i = 0; i < 12; i++) {
              await Future<void>.delayed(const Duration(milliseconds: 10));
              final v = wet.value;
              expect(v, inInclusiveRange(0.2 - 1e-3, 0.8 + 1e-3));
              seen.add(v);
            }
            expect(seen.length, greaterThan(1), reason: 'it oscillates');
          });

          test('a parameter of a voice whose sound lacks the filter', () {
            final h = soloud.play(sound);
            expect(
              () => filter.params.first.on(sound.filters, h).value,
              throwsA(isA<SoLoudFilterNotFoundException>()),
            );
          });

          test('a handle that is not a voice', () {
            f.activate();
            const bogus = SoundHandle(0x0BAD0BAD);
            final wet = filter.params.first.on(sound.filters, bogus);
            final notFound = throwsA(
              isA<SoLoudSoundHandleNotFoundCppException>(),
            );
            expect(() => wet.value, notFound);
            expect(() => wet.value = 0.5, notFound);
            expect(
              () => wet.fadeFilterParameter(to: 0, time: Duration.zero),
              notFound,
            );
            expect(
              () => wet.oscillateFilterParameter(
                from: 0,
                to: 1,
                time: Duration.zero,
              ),
              notFound,
            );
          });

          test('a disposed sound', () async {
            await soloud.disposeSource(sound);
            final notFound = throwsA(
              isA<SoLoudSoundHashNotFoundCppException>(),
            );
            expect(f.activate, notFound);
            expect(f.deactivate, notFound);
            expect(f.isActive, isFalse);
          });
        },
      );
    });
  }

  group('limits', () {
    test('at most 8 filters can be active at once', () {
      final active = <FilterBase>[];
      addTearDown(() {
        for (final f in active) {
          f.deactivate();
        }
      });
      for (final filter in _filters.take(8)) {
        filter.global.activate();
        active.add(filter.global);
      }
      expect(
        _filters[8].global.activate,
        throwsA(isA<SoLoudMaxFilterNumberReachedException>()),
      );
    });

    test('deactivating keeps the other filters and their values', () {
      final echo = _globals.echoFilter..activate();
      final lofi = _globals.lofiFilter..activate();
      final robot = _globals.robotizeFilter..activate();
      expect([echo.index, lofi.index, robot.index], [0, 1, 2]);
      _globals.robotizeFilter.wet.value = 0.4;
      lofi.deactivate();
      expect([echo.index, lofi.index, robot.index], [0, -1, 1]);
      expect(_globals.robotizeFilter.wet.value, closeTo(0.4, 1e-4));
      echo.deactivate();
      robot.deactivate();
    });

    test('disposeAllSources deactivates the global filters', () async {
      _globals.echoFilter.activate();
      await soloud.disposeAllSources();
      expect(_globals.echoFilter.isActive, isFalse);
    });
  });

  group('parametric EQ', () {
    final eq = _globals.parametricEqFilter;
    setUp(eq.activate);
    tearDown(() {
      if (eq.isActive) eq.deactivate();
    });

    test('a band index outside 0..63 throws ArgumentError', () {
      expect(() => eq.bandGain(64), throwsArgumentError);
      expect(() => eq.bandFreq(-1), throwsArgumentError);
      expect(() => eq.bandQ(64), throwsArgumentError);
    });

    test('stftWindowSize takes only powers of two in 32..4096', () {
      expect(eq.stftWindowSize.value, 1024);
      eq.stftWindowSize.value = 2048;
      expect(eq.stftWindowSize.value, 2048);
      eq.stftWindowSize.value = 1000;
      expect(eq.stftWindowSize.value, 2048);
      eq.stftWindowSize.value = 16;
      expect(eq.stftWindowSize.value, 2048);
    });

    test('the gain of every band can be set', () {
      for (var band = 0; band < 3; band++) {
        eq.bandGain(band).value = 0.5 + band;
      }
      for (var band = 0; band < 3; band++) {
        expect(eq.bandGain(band).value, closeTo(0.5 + band, 1e-4));
      }
    });

    test('getDefault matches the native band layout for any band count', () {
      for (final bands in [1, 2, 3, 8, 64]) {
        eq.numBands.value = bands.toDouble();
        for (final band in {0, 1, bands - 1, bands, 63}) {
          if (band > 63) continue;
          final freq = ParametricEqParam.bandFreq(band);
          final q = ParametricEqParam.bandQ(band);
          expect(
            eq.bandFreq(band).value,
            closeTo(ParametricEqParam.getDefault(freq, bands: bands), 1e-2),
            reason: 'band $band of $bands frequency',
          );
          expect(
            eq.bandQ(band).value,
            closeTo(ParametricEqParam.getDefault(q, bands: bands), 1e-4),
            reason: 'band $band of $bands Q',
          );
        }
      }
    });
  });

  group('custom filter', () {
    test('a global slot activates, reads its wet and deactivates', () {
      final slot = _globals.customFilter(0);
      expect(slot.isActive, isFalse);
      slot.activate();
      expect(slot.isActive, isTrue);
      expect(slot.wet.value, closeTo(1, 1e-4));
      slot.wet.value = 0.5;
      expect(slot.wet.value, closeTo(0.5, 1e-4));
      slot.deactivate();
      expect(slot.isActive, isFalse);
      expect(
        () => slot.wet.value,
        throwsA(isA<SoLoudFilterNotFoundException>()),
      );
    });

    test('two slots are separate filters', () {
      final a = _globals.customFilter(1)..activate();
      final b = _globals.customFilter(2)..activate();
      addTearDown(() {
        a.deactivate();
        b.deactivate();
      });
      a.wet.value = 0.2;
      expect(b.wet.value, closeTo(1, 1e-4));
      expect(a.activate, throwsA(isA<SoLoudFilterAlreadyAddedException>()));
    });

    test('configureParameters defines the user parameters', () {
      final slot = _globals.customFilter(3)
        ..configureParameters(const [
          CustomFilterParamSpec(
            name: 'gain',
            min: 0,
            max: 2,
            defaultValue: 1.5,
          ),
        ])
        ..activate();
      addTearDown(slot.deactivate);
      expect(slot.param(1).value, closeTo(1.5, 1e-4));
      slot.param(1, max: 2).value = 0.75;
      expect(slot.param(1).value, closeTo(0.75, 1e-4));
    });

    test('15 user parameters can be configured and used', () {
      final slot = _globals.customFilter(4)
        ..activate()
        ..configureParameters([
          for (var i = 0; i < CustomFilterSlot.maxUserParams; i++)
            CustomFilterParamSpec(name: 'p$i', min: 0, max: 1, defaultValue: 0),
        ]);
      addTearDown(slot.deactivate);
      slot.param(CustomFilterSlot.maxUserParams, max: 1).value = 0.25;
      expect(
        slot.param(CustomFilterSlot.maxUserParams).value,
        closeTo(0.25, 1e-4),
      );
    });

    test('more than 15 user parameters throw ArgumentError', () {
      expect(
        () => _globals.customFilter(4).configureParameters([
          for (var i = 0; i < 16; i++)
            CustomFilterParamSpec(name: 'p$i', min: 0, max: 1, defaultValue: 0),
        ]),
        throwsArgumentError,
      );
    });

    test('a slot index outside 0..7 is rejected', () {
      expect(() => _globals.customFilter(8), throwsA(isA<AssertionError>()));
      expect(() => _globals.customFilter(-1), throwsA(isA<AssertionError>()));
    });

    test(
      'a slot on a single sound',
      skip: kIsWeb
          ? 'filters for single sounds are not supported on the web'
          : null,
      () async {
        final sound = await loadTestWaveform();
        final slot = sound.filters.customFilter(0)..activate();
        expect(slot.isActive, isTrue);
        expect(_globals.customFilter(0).isActive, isFalse);
        final h = soloud.play(sound);
        expect(slot.wet(soundHandle: h).value, closeTo(1, 1e-4));
        slot.deactivate();
      },
    );
  });

  group('a parameter of a sound without the voice handle', () {
    test(
      'throws ArgumentError instead of addressing the global filter',
      skip: kIsWeb
          ? 'filters for single sounds are not supported on the web'
          : null,
      () async {
        final sound = await loadTestWaveform();
        sound.filters.echoFilter.activate();
        // The global filter of the same type: it must not be touched.
        _globals.echoFilter.activate();
        addTearDown(_globals.echoFilter.deactivate);
        final before = _globals.echoFilter.delay.value;
        soloud.play(sound);

        final delay = sound.filters.echoFilter.delay();
        expect(() => delay.value, throwsArgumentError);
        expect(() => delay.value = 0.2, throwsArgumentError);
        expect(
          () => delay.fadeFilterParameter(to: 0.2, time: Duration.zero),
          throwsArgumentError,
        );
        expect(
          () => delay.oscillateFilterParameter(
            from: 0.1,
            to: 0.2,
            time: const Duration(seconds: 1),
          ),
          throwsArgumentError,
        );
        final custom = sound.filters.customFilter(0)..activate();
        expect(() => custom.wet().value, throwsArgumentError);
        expect(() => custom.param(1).value = 0.5, throwsArgumentError);
        expect(_globals.echoFilter.delay.value, before);
      },
    );

    test('a bus parameter needs no handle', () {
      final bus = soloud.createMixingBus()..playOnEngine();
      addTearDown(bus.dispose);
      final echo = bus.filters.echoFilter..activate();
      echo.delay().value = 0.2;
      expect(echo.delay().value, closeTo(0.2, 1e-4));
    });
  });

  group('before init', () {
    test('filter calls throw', () {
      soloud.deinit();
      expect(
        _globals.echoFilter.activate,
        throwsA(isA<SoLoudBackendNotInitedException>()),
      );
      expect(
        () => _globals.echoFilter.wet.value,
        throwsA(isA<SoLoudBackendNotInitedException>()),
      );
      expect(
        () => _globals.echoFilter.wet.fadeFilterParameter(
          to: 0,
          time: Duration.zero,
        ),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
      expect(_globals.echoFilter.isActive, isFalse);
    });
  });
}

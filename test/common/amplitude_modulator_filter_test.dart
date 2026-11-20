// The amplitude modulator filter through the Dart API, with the real engine:
// as a global filter (with a spectral check of the captured engine output),
// per sound and on a mixing bus. The DSP itself is covered natively by
// amplitude_modulator_test.cpp.
@Tags(['device'])
library;

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

const _sampleRate = 44100.0; // SoLoud.init() default.

void main() {
  setUpEngineTests();

  test('global: parameters, spectrum, fade and oscillation', () async {
    final filter = SoLoud.instance.filters.amplitudeModulatorFilter;
    expect(filter.queryWet.def, 1);
    expect(filter.queryFrequency.min, 0.1);
    expect(filter.queryFrequency.max, 20000);
    expect(filter.queryFrequency.def, 440);

    filter.activate();
    expect(filter.isActive, isTrue);
    expect(filter.wet.value, closeTo(1, 1e-6));
    expect(filter.frequency.value, closeTo(440, 1e-3));

    // A 1 kHz sine source.
    final sound = await loadTestWaveform(frequency: 1000);
    SoLoud.instance.play(sound, looping: true);

    // wet = 0 leaves the 1 kHz tone alone.
    filter.wet.value = 0;
    filter.frequency.value = 16000;
    expect(filter.frequency.value, closeTo(16000, 1e-3));
    await Future<void>.delayed(const Duration(milliseconds: 200));
    final dry = _tones(await _captureChannel0(frames: 22050));
    expect(dry[15000], lessThan(dry[1000]! * 0.01), reason: 'dry: $dry');
    expect(dry[17000], lessThan(dry[1000]! * 0.01), reason: 'dry: $dry');

    // Full wet: sin(1 kHz) * sin(16 kHz) = 0.5 cos(15 kHz) - 0.5 cos(17 kHz).
    filter.wet.value = 1;
    await Future<void>.delayed(const Duration(milliseconds: 200));
    final wet = _tones(await _captureChannel0(frames: 22050));
    final a15 = wet[15000]!;
    expect(a15, greaterThan(0), reason: 'wet: $wet');
    expect((a15 - wet[17000]!).abs(), lessThan(0.1 * a15), reason: '$wet');
    expect(wet[1000], lessThan(a15 * 0.01), reason: 'input kept: $wet');
    expect(wet[16000], lessThan(a15 * 0.01), reason: 'carrier kept: $wet');

    // Out-of-range values are rejected by the Dart API.
    filter.frequency.value = 25000;
    filter.wet.value = 1.5;
    expect(filter.frequency.value, closeTo(16000, 1e-3));
    expect(filter.wet.value, closeTo(1, 1e-6));

    filter.frequency.fadeFilterParameter(
      to: 1000,
      time: const Duration(milliseconds: 200),
    );
    expect(
      await waitUntil(() => (filter.frequency.value - 1000).abs() < 1e-2),
      isTrue,
      reason: 'frequency fade ended at ${filter.frequency.value}',
    );

    filter.wet.oscillateFilterParameter(
      from: 0.2,
      to: 0.8,
      time: const Duration(milliseconds: 400),
    );
    // The oscillation starts on the engine's next mix.
    await waitUntil(() => filter.wet.value <= 0.8 + 1e-3);
    final samples = <double>[];
    for (var i = 0; i < 12; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 70));
      samples.add(filter.wet.value);
    }
    expect(samples, everyElement(inInclusiveRange(0.2 - 1e-3, 0.8 + 1e-3)));
    expect(
      samples.reduce(math.max) - samples.reduce(math.min),
      greaterThan(0.2),
      reason: 'wet does not oscillate: $samples',
    );

    filter.deactivate();
    expect(filter.isActive, isFalse);
    await SoLoud.instance.disposeSource(sound);
  });

  test(
    'per sound and on a mixing bus: set, get and fade',
    skip: kIsWeb
        ? 'filters for single sounds are not supported on the web'
        : null,
    () async {
      final sound = await loadTestWaveform(frequency: 1000);

      final filter = sound.filters.amplitudeModulatorFilter..activate();
      expect(filter.isActive, isTrue);
      final handle = SoLoud.instance.play(sound, looping: true);
      filter.frequency(soundHandle: handle).value = 16000;
      filter.wet(soundHandle: handle).value = 0.5;
      expect(filter.frequency(soundHandle: handle).value, closeTo(16000, 1e-3));
      expect(filter.wet(soundHandle: handle).value, closeTo(0.5, 1e-6));

      filter
          .frequency(soundHandle: handle)
          .fadeFilterParameter(
            to: 200,
            time: const Duration(milliseconds: 200),
          );
      expect(
        await waitUntil(
          () =>
              (filter.frequency(soundHandle: handle).value - 200).abs() < 1e-2,
        ),
        isTrue,
        reason: 'sound frequency fade did not complete',
      );
      filter
          .wet(soundHandle: handle)
          .oscillateFilterParameter(
            from: 0,
            to: 1,
            time: const Duration(milliseconds: 200),
          );
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(filter.wet(soundHandle: handle).value, inInclusiveRange(0, 1));
      await SoLoud.instance.stop(handle);
      filter.deactivate();
      expect(filter.isActive, isFalse);

      final bus = SoLoud.instance.createMixingBus(name: 'AM Bus');
      final busFilter = bus.filters.amplitudeModulatorFilter..activate();
      expect(busFilter.isActive, isTrue);
      final busHandle = bus.playOnEngine();
      bus.play(sound, looping: true);
      busFilter.frequency(soundHandle: busHandle).value = 16000;
      expect(
        busFilter.frequency(soundHandle: busHandle).value,
        closeTo(16000, 1e-3),
      );
      busFilter
          .wet(soundHandle: busHandle)
          .fadeFilterParameter(
            to: 0.25,
            time: const Duration(milliseconds: 200),
          );
      expect(
        await waitUntil(
          () =>
              (busFilter.wet(soundHandle: busHandle).value - 0.25).abs() < 1e-3,
        ),
        isTrue,
        reason: 'bus wet fade did not complete',
      );
      busFilter.deactivate();
      expect(busFilter.isActive, isFalse);
      bus.dispose();
      await SoLoud.instance.disposeSource(sound);
    },
  );
}

/// Captures the engine output as PCM F32 and returns [frames] frames of the
/// first channel, skipping the first 4096 frames to avoid any transition.
Future<Float32List> _captureChannel0({required int frames}) async {
  const channels = 2;
  const skip = 4096;
  final bytes = BytesBuilder(copy: false);
  final done = Completer<void>();
  final subscription = SoLoud.instance.startMixerOutputStream().listen((c) {
    bytes.add(c);
    if (bytes.length >= (frames + skip) * channels * 4 && !done.isCompleted) {
      done.complete();
    }
  });
  await done.future.timeout(const Duration(seconds: 5));
  SoLoud.instance.stopMixerOutputStream();
  await subscription.cancel();

  // Copy to get a 4-byte aligned buffer.
  final all = Uint8List.fromList(bytes.takeBytes()).buffer.asFloat32List();
  return Float32List.fromList([
    for (var i = 0; i < frames; i++) all[(skip + i) * channels],
  ]);
}

/// Hann-windowed amplitudes of the tones of interest.
Map<int, double> _tones(Float32List x) => {
  for (final f in const [1000, 15000, 16000, 17000])
    f: _toneAmplitude(x, f.toDouble()),
};

double _toneAmplitude(Float32List x, double freq) {
  final n = x.length;
  var re = 0.0;
  var im = 0.0;
  var wsum = 0.0;
  for (var i = 0; i < n; i++) {
    final w = 0.5 - 0.5 * math.cos(2 * math.pi * i / n);
    final ph = 2 * math.pi * freq * i / _sampleRate;
    re += w * x[i] * math.cos(ph);
    im -= w * x[i] * math.sin(ph);
    wsum += w;
  }
  return 2 * math.sqrt(re * re + im * im) / wsum;
}

// Visualization of the engine output and of a mixing bus, with the real
// engine: the FFT of a generated tone peaks in the expected bin, the wave is
// the tone, FFT smoothing and the decibel range shape the bins, the window
// size, kind and channel selection shape the packets, invalid settings
// throw, and nothing is emitted while visualization is disabled.
@Tags(['device'])
library;

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// The engine sample rate (the default of `SoLoud.init`).
const _sampleRate = 44100;

/// FFT window used by most tests, and the bin the test tone falls in.
const _window = 1024;
const _bin = 40;

/// A tone exactly at the center of FFT bin [_bin] of a [_window] window.
const _toneHz = _bin * _sampleRate / _window;

/// Output peak of the test tone played centered at global volume 0.1: a
/// waveform peaks at 0.5, and centering a mono voice on stereo scales each
/// channel by sqrt(1/2).
const _centeredPeak = 0.1 * 0.5 * math.sqrt1_2;

/// Collects the packets of [events] until [count] arrived (or a timeout).
Future<List<AudioVisualizationData>> _packets(
  Stream<AudioVisualizationData> events, {
  int count = 5,
  Duration timeout = const Duration(seconds: 5),
}) async {
  final out = <AudioVisualizationData>[];
  final done = Completer<void>();
  final subscription = events.listen((data) {
    out.add(data);
    if (out.length >= count && !done.isCompleted) done.complete();
  });
  try {
    await done.future.timeout(timeout, onTimeout: () {});
  } finally {
    await subscription.cancel();
  }
  return out;
}

int _argMax(Float32List data) {
  var best = 0;
  for (var i = 1; i < data.length; i++) {
    if (data[i] > data[best]) best = i;
  }
  return best;
}

double _peak(Float32List data) =>
    data.fold<double>(0, (p, s) => math.max(p, s.abs()));

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  late AudioSource tone;
  setUp(() async => tone = await loadTestWaveform(frequency: _toneHz));
  tearDown(() {
    // Restore the analyzer's process-wide settings for the next test.
    if (soloud.isInitialized) {
      soloud
        ..setFftSmoothing(0.8)
        ..setFftDecibelRange(-100, -30);
    }
  });

  group('engine output', () {
    test('is disabled after init and toggles', () {
      expect(soloud.getVisualizationEnabled(), isFalse);
      soloud.setVisualizationEnabled(true);
      expect(soloud.getVisualizationEnabled(), isTrue);
      soloud.setVisualizationEnabled(false);
      expect(soloud.getVisualizationEnabled(), isFalse);
    });

    test('emits nothing while disabled', () async {
      soloud.play(tone);
      final packets = await _packets(
        soloud.audioVisualizationEvents,
        timeout: const Duration(milliseconds: 500),
      );
      expect(packets, isEmpty);
    });

    test('FFT of a tone peaks in the expected bin', () async {
      soloud
        ..setFftSmoothing(0)
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        )
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      final last = packets.last;
      expect(last.channelCount, 1);
      expect(last.wave, isEmpty, reason: 'FFT only');
      final fft = last.fftData!;
      expect(fft.length, _window ~/ 2);
      expect(fft.every((v) => v >= 0 && v <= 1), isTrue);
      expect(_argMax(fft), inInclusiveRange(_bin - 1, _bin + 1));
      expect(fft[_bin], greaterThan(0.5));
      // Far from the tone the bins are at (or near) the floor.
      expect(fft[_bin * 4], lessThan(fft[_bin] / 2));
    });

    test('wave is the tone, in -1..1', () async {
      soloud
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.wave,
        )
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      final last = packets.last;
      expect(last.fft, isEmpty, reason: 'wave only');
      final wave = last.waveData!;
      expect(wave.length, _window);
      expect(_peak(wave), closeTo(_centeredPeak, 0.005));
      // _bin periods in the window: count the rising zero crossings.
      var crossings = 0;
      for (var i = 1; i < wave.length; i++) {
        if (wave[i - 1] < 0 && wave[i] >= 0) crossings++;
      }
      expect(crossings, inInclusiveRange(_bin - 1, _bin + 1));
    });

    test('waveAndFft carries both, with the window size requested', () async {
      soloud
        ..setVisualizationEnabled(true, windowSize: 512)
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents);
      expect(packets, isNotEmpty);
      expect(packets.last.waveData!.length, 512);
      expect(packets.last.fftData!.length, 256);
    });

    test('channel all gives one list per output channel', () async {
      soloud
        ..setVisualizationEnabled(true, channel: VisualizationChannel.all)
        ..play(tone, pan: -1);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      final last = packets.last;
      expect(last.channelCount, 2);
      expect(last.wave, hasLength(2));
      expect(last.fft, hasLength(2));
      // Panned hard left: the left channel carries the tone, the right not.
      expect(_peak(last.wave[0]), greaterThan(0.04));
      expect(_peak(last.wave[1]), lessThan(0.01));
    });

    test('a single channel selects that channel', () async {
      soloud
        ..setVisualizationEnabled(
          true,
          kind: VisualizationKind.wave,
          channel: 1,
        )
        ..play(tone, pan: 1);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      expect(packets.last.channelCount, 1);
      expect(_peak(packets.last.waveData!), greaterThan(0.04));
    });

    test('invalid settings throw and leave it disabled', () {
      for (final size in [64, 100, 1000, 16384]) {
        expect(
          () => soloud.setVisualizationEnabled(true, windowSize: size),
          throwsA(isA<SoLoudCppException>()),
          reason: 'windowSize $size',
        );
      }
      for (final channel in [2, 5, -3]) {
        expect(
          () => soloud.setVisualizationEnabled(true, channel: channel),
          throwsA(isA<SoLoudCppException>()),
          reason: 'channel $channel',
        );
      }
      expect(soloud.getVisualizationEnabled(), isFalse);
    });

    test('disabling stops the packets', () async {
      soloud
        ..setVisualizationEnabled(true)
        ..play(tone);
      expect(await _packets(soloud.audioVisualizationEvents), isNotEmpty);
      soloud.setVisualizationEnabled(false);
      // A packet already queued may still arrive: let it drain.
      await Future<void>.delayed(const Duration(milliseconds: 100));
      final after = await _packets(
        soloud.audioVisualizationEvents,
        timeout: const Duration(milliseconds: 500),
      );
      expect(after, isEmpty);
    });

    test('getApproximateVolume follows the output peak only while '
        'visualization is enabled', () async {
      soloud.play(tone);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(soloud.getApproximateVolume(0), 0, reason: 'disabled');
      soloud.setVisualizationEnabled(true);
      await _packets(soloud.audioVisualizationEvents);
      expect(soloud.getApproximateVolume(0), closeTo(_centeredPeak, 0.005));
      expect(soloud.getApproximateVolume(1), closeTo(_centeredPeak, 0.005));
      expect(soloud.getApproximateVolume(99), 0, reason: 'no such channel');
      soloud.setVisualizationEnabled(false);
      expect(soloud.getApproximateVolume(0), 0);
    });
  });

  group('FFT smoothing', () {
    test('1 keeps the bins at their initial zero', () async {
      soloud
        ..setFftSmoothing(1)
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        )
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      expect(packets.last.fftData!.every((v) => v == 0), isTrue);
    });

    test('0 follows the signal at once', () async {
      soloud
        ..setFftSmoothing(0)
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        );
      final h = soloud.play(tone);
      final playing = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(playing.last.fftData![_bin], greaterThan(0.5));
      // Keep the device busy with silence so packets keep coming.
      soloud.play(await loadSilence(duration: const Duration(seconds: 5)));
      await soloud.stop(h);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      final silent = await _packets(soloud.audioVisualizationEvents, count: 3);
      expect(silent, isNotEmpty);
      expect(silent.last.fftData![_bin], 0);
    });

    test('values out of 0..1 are clamped', () async {
      soloud
        ..setFftSmoothing(5)
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        )
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      // Clamped to 1: nothing moves.
      expect(packets.last.fftData!.every((v) => v == 0), isTrue);
    });
  });

  group('FFT decibel range', () {
    test('defaults to -100..-30 and round-trips', () {
      expect(soloud.minDecibels, -100);
      expect(soloud.maxDecibels, -30);
      soloud.setFftDecibelRange(-90, -10);
      expect(soloud.minDecibels, -90);
      expect(soloud.maxDecibels, -10);
    });

    test('min >= max throws and keeps the range', () {
      expect(
        () => soloud.setFftDecibelRange(-30, -30),
        throwsA(isA<ArgumentError>()),
      );
      expect(
        () => soloud.setFftDecibelRange(0, -10),
        throwsA(isA<ArgumentError>()),
      );
      expect(soloud.minDecibels, -100);
      expect(soloud.maxDecibels, -30);
    });

    Future<Float32List> fftWithRange(double min, double max) async {
      soloud
        ..setFftSmoothing(0)
        ..setFftDecibelRange(min, max)
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        )
        ..play(tone);
      final packets = await _packets(soloud.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      return packets.last.fftData!;
    }

    test('a range below the signal saturates the tone bin', () async {
      final fft = await fftWithRange(-300, -290);
      expect(fft[_bin], 1);
    });

    test('a range above the signal zeroes every bin', () async {
      final fft = await fftWithRange(0, 10);
      expect(fft.every((v) => v == 0), isTrue);
    });
  });

  group('bus', () {
    late Bus bus;
    setUp(() {
      bus = soloud.createMixingBus()..playOnEngine();
    });
    tearDown(() {
      if (Buses().buses.contains(bus)) bus.dispose();
    });

    test('is disabled by default and toggles', () {
      expect(bus.getVisualizationEnabled(), isFalse);
      bus.setVisualizationEnabled(true);
      expect(bus.getVisualizationEnabled(), isTrue);
      bus.setVisualizationEnabled(false);
      expect(bus.getVisualizationEnabled(), isFalse);
    });

    test('FFT of a tone played through the bus peaks in the expected bin, '
        'and the engine stream stays silent', () async {
      final engineOutput = <AudioVisualizationData>[];
      final sub = soloud.audioVisualizationEvents.listen(engineOutput.add);
      addTearDown(sub.cancel);
      soloud.setFftSmoothing(0);
      bus
        ..setVisualizationEnabled(
          true,
          windowSize: _window,
          kind: VisualizationKind.fft,
        )
        ..play(tone);
      final packets = await _packets(bus.audioVisualizationEvents, count: 8);
      expect(packets, isNotEmpty);
      final fft = packets.last.fftData!;
      expect(_argMax(fft), inInclusiveRange(_bin - 1, _bin + 1));
      expect(engineOutput, isEmpty, reason: 'engine output not enabled');
    });

    test('getChannelVolume reads the peak mixed into the bus', () async {
      bus
        ..setVisualizationEnabled(true)
        ..play(tone, volume: 0.5);
      await _packets(bus.audioVisualizationEvents);
      // Before the bus volume and the global volume: 0.5 * the waveform's
      // centered peak.
      expect(bus.getChannelVolume(0), closeTo(0.5 * 0.5 * math.sqrt1_2, 0.01));
      expect(bus.getChannelVolume(5), 0, reason: 'no such channel');
      bus.setVisualizationEnabled(false);
      expect(bus.getChannelVolume(0), 0);
    });

    test('invalid settings throw', () {
      expect(
        () => bus.setVisualizationEnabled(true, windowSize: 300),
        throwsA(isA<SoLoudCppException>()),
      );
      expect(
        () => bus.setVisualizationEnabled(true, channel: 2),
        throwsA(isA<SoLoudCppException>()),
      );
      expect(bus.getVisualizationEnabled(), isFalse);
    });

    test('dispose closes the stream', () async {
      bus.setVisualizationEnabled(true);
      final done = Completer<void>();
      bus.audioVisualizationEvents.listen(null, onDone: done.complete);
      bus.dispose();
      await done.future.timeout(const Duration(seconds: 2));
      expect(
        () => bus.setVisualizationEnabled(true),
        throwsA(isA<SoLoudBusDisposedDartException>()),
      );
    });
  });
}

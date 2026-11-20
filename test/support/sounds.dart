// Test sounds generated in memory, so host tests need no asset files.
//
// `wavBytes` builds a 16-bit PCM WAV file (sine, noise or silence) and the
// `load*` helpers hand it to `SoLoud.loadMem`, or create a synthetic
// waveform with `SoLoud.loadWaveform`. Every loader needs an initialized
// engine.

import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

/// The content of a generated test sound.
enum TestSignal {
  /// A sine tone at the requested frequency.
  sine,

  /// White noise from a seeded generator (reproducible).
  noise,

  /// Digital silence.
  silence,
}

/// The samples of a generated sound, interleaved, in -1..1.
Float32List testSamples({
  TestSignal signal = TestSignal.sine,
  Duration duration = const Duration(seconds: 1),
  int sampleRate = 44100,
  int channels = 1,
  double frequency = 440,
  double amplitude = 0.5,
  int seed = 1,
}) {
  final frames = duration.inMicroseconds * sampleRate ~/ 1000000;
  final out = Float32List(frames * channels);
  final random = math.Random(seed);
  for (var f = 0; f < frames; f++) {
    final value = switch (signal) {
      TestSignal.sine =>
        amplitude * math.sin(2 * math.pi * frequency * f / sampleRate),
      TestSignal.noise => amplitude * (random.nextDouble() * 2 - 1),
      TestSignal.silence => 0.0,
    };
    for (var c = 0; c < channels; c++) {
      out[f * channels + c] = value;
    }
  }
  return out;
}

/// A complete 16-bit PCM WAV file of [signal]: [duration] long, at
/// [sampleRate] Hz, with [channels] channels (all carrying the same signal).
Uint8List wavBytes({
  TestSignal signal = TestSignal.sine,
  Duration duration = const Duration(seconds: 1),
  int sampleRate = 44100,
  int channels = 1,
  double frequency = 440,
  double amplitude = 0.5,
  int seed = 1,
}) {
  final samples = testSamples(
    signal: signal,
    duration: duration,
    sampleRate: sampleRate,
    channels: channels,
    frequency: frequency,
    amplitude: amplitude,
    seed: seed,
  );
  final dataBytes = samples.length * 2;
  final b = ByteData(44 + dataBytes);
  void tag(int at, String s) {
    for (var i = 0; i < 4; i++) {
      b.setUint8(at + i, s.codeUnitAt(i));
    }
  }

  tag(0, 'RIFF');
  b.setUint32(4, 36 + dataBytes, Endian.little);
  tag(8, 'WAVE');
  tag(12, 'fmt ');
  b
    ..setUint32(16, 16, Endian.little) // fmt chunk size
    ..setUint16(20, 1, Endian.little) // PCM
    ..setUint16(22, channels, Endian.little)
    ..setUint32(24, sampleRate, Endian.little)
    ..setUint32(28, sampleRate * channels * 2, Endian.little) // byte rate
    ..setUint16(32, channels * 2, Endian.little) // block align
    ..setUint16(34, 16, Endian.little); // bits per sample
  tag(36, 'data');
  b.setUint32(40, dataBytes, Endian.little);
  for (var i = 0; i < samples.length; i++) {
    final s = (samples[i] * 32767).round().clamp(-32768, 32767);
    b.setInt16(44 + i * 2, s, Endian.little);
  }
  return b.buffer.asUint8List();
}

var _generatedCount = 0;

/// Loads a generated WAV (see [wavBytes]) with `SoLoud.loadMem`. Each call
/// uses a distinct path, so the same sound can be loaded more than once.
Future<AudioSource> loadTestSound({
  TestSignal signal = TestSignal.sine,
  Duration duration = const Duration(seconds: 1),
  int sampleRate = 44100,
  int channels = 1,
  double frequency = 440,
  double amplitude = 0.5,
  int seed = 1,
  LoadMode mode = LoadMode.memory,
  bool autoDispose = false,
}) {
  return SoLoud.instance.loadMem(
    'generated_${signal.name}_${_generatedCount++}.wav',
    wavBytes(
      signal: signal,
      duration: duration,
      sampleRate: sampleRate,
      channels: channels,
      frequency: frequency,
      amplitude: amplitude,
      seed: seed,
    ),
    mode: mode,
    autoDispose: autoDispose,
  );
}

/// A generated sine tone, loaded with `SoLoud.loadMem`.
Future<AudioSource> loadSine({
  Duration duration = const Duration(seconds: 1),
  double frequency = 440,
  int sampleRate = 44100,
  int channels = 1,
  double amplitude = 0.5,
}) => loadTestSound(
  duration: duration,
  frequency: frequency,
  sampleRate: sampleRate,
  channels: channels,
  amplitude: amplitude,
);

/// Generated white noise, loaded with `SoLoud.loadMem`.
Future<AudioSource> loadNoise({
  Duration duration = const Duration(seconds: 1),
  int sampleRate = 44100,
  int channels = 1,
  double amplitude = 0.5,
  int seed = 1,
}) => loadTestSound(
  signal: TestSignal.noise,
  duration: duration,
  sampleRate: sampleRate,
  channels: channels,
  amplitude: amplitude,
  seed: seed,
);

/// Generated silence, loaded with `SoLoud.loadMem`.
Future<AudioSource> loadSilence({
  Duration duration = const Duration(seconds: 1),
  int sampleRate = 44100,
  int channels = 1,
}) => loadTestSound(
  signal: TestSignal.silence,
  duration: duration,
  sampleRate: sampleRate,
  channels: channels,
);

/// An endless synthetic [waveform] at [frequency] Hz (`SoLoud.loadWaveform`).
Future<AudioSource> loadTestWaveform({
  WaveForm waveform = WaveForm.sin,
  double frequency = 440,
  bool superWave = false,
  double scale = 1,
  double detune = 0,
}) async {
  final sound = await SoLoud.instance.loadWaveform(
    waveform,
    superWave,
    scale,
    detune,
  );
  SoLoud.instance.setWaveformFreq(sound, frequency);
  return sound;
}

/// Polls [condition] every [interval] until it holds or [timeout] passes,
/// and returns its last value.
Future<bool> waitUntil(
  bool Function() condition, {
  Duration timeout = const Duration(seconds: 3),
  Duration interval = const Duration(milliseconds: 10),
}) async {
  final deadline = DateTime.now().add(timeout);
  while (!condition()) {
    if (DateTime.now().isAfter(deadline)) return condition();
    await Future<void>.delayed(interval);
  }
  return true;
}

/// Prepares a host test file to start the real engine: initializes the test
/// binding and answers path_provider (which `SoLoud.init` asks for its temp
/// directory) with a temporary directory deleted after the file's tests.
///
/// Call it first in `main()`. Tests that manage `init`/`deinit` themselves
/// only need this; the others can use [setUpEngineTests].
void prepareEngineHost() {
  TestWidgetsFlutterBinding.ensureInitialized();
  // In the browser SoLoud.init() does not ask path_provider for anything.
  if (kIsWeb) return;
  final temp = Directory.systemTemp.createTempSync('soloud_test');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );
  tearDownAll(() {
    if (temp.existsSync()) temp.deleteSync(recursive: true);
  });
}

/// [prepareEngineHost], then an engine initialized before every test (at a
/// low [globalVolume]) and deinitialized after it.
void setUpEngineTests({double globalVolume = 0.1}) {
  prepareEngineHost();
  setUp(() => initTestEngine(globalVolume: globalVolume));
  tearDown(deinitTestEngine);
}

/// Initializes the engine for a device test, with a low global volume.
Future<void> initTestEngine({double globalVolume = 0.1}) async {
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(globalVolume);
}

/// Deinitializes the engine if it is running (for `tearDown`).
void deinitTestEngine() {
  if (SoLoud.instance.isInitialized) SoLoud.instance.deinit();
}

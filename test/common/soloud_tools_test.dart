// SoLoudTools.createNotes: twelve waveform sources tuned to the semitones of
// an octave, starting at 55 Hz * 2^octave. The pitch is checked on the real
// mixer output, captured with startMixerOutputStream.
@TestOn('vm')
@Tags(['device'])
library;

import 'dart:async';
import 'dart:io';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

// The default engine rate of SoLoud.init.
const _sampleRate = 44100;

/// Plays [note] alone and returns the frequency of the captured mono output,
/// estimated from its rising zero crossings.
Future<double> _measuredFrequency(AudioSource note) async {
  final soloud = SoLoud.instance;
  final samples = <double>[];
  final enough = Completer<void>();
  final subscription = soloud
      .startMixerOutputStream(sampleRate: _sampleRate, channels: 1)
      .listen((chunk) {
        final floats = Float32List.sublistView(
          chunk,
          0,
          chunk.lengthInBytes ~/ 4 * 4,
        );
        samples.addAll(floats);
        // ~0.5 s once the voice is audible.
        if (samples.length > _sampleRate && !enough.isCompleted) {
          enough.complete();
        }
      });
  final handle = soloud.play(note);
  try {
    await enough.future.timeout(const Duration(seconds: 5));
  } finally {
    await soloud.stop(handle);
    soloud.stopMixerOutputStream();
    await subscription.cancel();
  }

  // Skip the first quarter, which may predate the voice or hold its start.
  final window = samples.sublist(samples.length ~/ 2);
  int? first;
  int? last;
  var crossings = 0;
  for (var i = 1; i < window.length; i++) {
    if (window[i - 1] < 0 && window[i] >= 0) {
      first ??= i;
      last = i;
      crossings++;
    }
  }
  if (first == null || crossings < 3) return 0;
  return (crossings - 1) * _sampleRate / (last! - first);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final temp = Directory.systemTemp.createTempSync('soloud_tools');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );

  setUpAll(() => SoLoud.instance.init(channels: Channels.mono));
  tearDownAll(() {
    SoLoud.instance.deinit();
    temp.deleteSync(recursive: true);
  });
  tearDown(SoLoud.instance.disposeAllSources);

  test('createNotes returns 12 distinct, loaded sources', () async {
    final notes = await SoLoudTools.createNotes();
    expect(notes, hasLength(12));
    expect(notes.map((n) => n.soundHash).toSet(), hasLength(12));
    for (final note in notes) {
      expect(note.soundHash.isValid, isTrue);
      expect(SoLoud.instance.activeSounds, contains(note));
    }
  });

  test('every octave from 0 to 4 is accepted', () async {
    for (var octave = 0; octave <= 4; octave++) {
      final notes = await SoLoudTools.createNotes(octave: octave);
      expect(notes, hasLength(12), reason: 'octave $octave');
      await SoLoud.instance.disposeAllSources();
    }
  });

  test('an octave outside 0..4 trips the assert', () async {
    await expectLater(
      SoLoudTools.createNotes(octave: 5),
      throwsA(isA<AssertionError>()),
    );
    await expectLater(
      SoLoudTools.createNotes(octave: -1),
      throwsA(isA<AssertionError>()),
    );
  });

  test('throws when the engine is not initialized', () async {
    SoLoud.instance.deinit();
    addTearDown(() => SoLoud.instance.init(channels: Channels.mono));
    await expectLater(
      SoLoudTools.createNotes(),
      throwsA(isA<SoLoudNotInitializedException>()),
    );
  });

  test('notes follow equal temperament from 55 Hz * 2^octave', () async {
    // Octave 2: A = 220 Hz. Check the root, the fifth and the last note.
    final notes = await SoLoudTools.createNotes(octave: 2, superwave: false);
    for (final index in [0, 7, 11]) {
      final expected = 220.0 * pow(2, index / 12);
      final measured = await _measuredFrequency(notes[index]);
      expect(
        measured,
        closeTo(expected, expected * 0.02),
        reason: 'note $index',
      );
    }
  });

  test('the waveform does not change the pitch', () async {
    final sine = await SoLoudTools.createNotes(octave: 1, superwave: false);
    final square = await SoLoudTools.createNotes(
      octave: 1,
      waveForm: WaveForm.square,
      superwave: false,
    );
    // Same pitch (110 Hz) whatever the waveform.
    expect(await _measuredFrequency(sine[0]), closeTo(110, 2.5));
    expect(await _measuredFrequency(square[0]), closeTo(110, 2.5));
  });
}

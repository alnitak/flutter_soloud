// Looking sources up and playing them in one call, with the real engine:
// findAudioSourceByHandle with live, ended, stopped, made-up handles and a
// disposed source; playSource (from a file on native, from a data: URL in
// the browser) with its parameters and auto-dispose; audioDeviceStartFailures
// republishing the native start-failure notification.
@Tags(['device'])
library;

import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_soloud/src/enums.dart' show PlayerStateNotification;
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// Writes [bytes] to a new temporary .wav file (native only) and returns its
/// path. The file is deleted after the test.
String writeTempWav(Uint8List bytes) {
  final dir = Directory.systemTemp.createTempSync('soloud_play_source');
  addTearDown(() async {
    if (SoLoud.instance.isInitialized) {
      await SoLoud.instance.disposeAllSources();
    }
    dir.deleteSync(recursive: true);
  });
  final file = File('${dir.path}/tone.wav')..writeAsBytesSync(bytes);
  return file.path;
}

/// A path where no file exists (native only).
String missingFilePath() =>
    '${Directory.systemTemp.path}/soloud_no_such_file_${DateTime.now().microsecondsSinceEpoch}.wav';

/// A handle no voice has ever had.
const _bogus = SoundHandle(0x0BAD0BAD);

void main() {
  setUpEngineTests(globalVolume: 0.05);
  final soloud = SoLoud.instance;

  group('findAudioSourceByHandle', () {
    test('finds the source of each live voice', () async {
      final a = await loadTestWaveform();
      final b = await loadTestWaveform(frequency: 880);
      final ha1 = soloud.play(a);
      final ha2 = soloud.play(a);
      final hb = soloud.play(b);
      expect(soloud.findAudioSourceByHandle(ha1), same(a));
      expect(soloud.findAudioSourceByHandle(ha2), same(a));
      expect(soloud.findAudioSourceByHandle(hb), same(b));
    });

    test('finds nothing for a handle that never existed', () {
      expect(soloud.findAudioSourceByHandle(_bogus), isNull);
      expect(soloud.findAudioSourceByHandle(const SoundHandle.error()), isNull);
    });

    test('finds nothing once the voice ended by itself', () async {
      final sound = await loadSine(duration: const Duration(milliseconds: 100));
      final h = soloud.play(sound);
      expect(soloud.findAudioSourceByHandle(h), same(sound));
      expect(
        await waitUntil(() => soloud.findAudioSourceByHandle(h) == null),
        isTrue,
      );
      expect(soloud.activeSounds, contains(sound), reason: 'not disposed');
    });

    test('finds nothing once the voice is stopped', () async {
      final sound = await loadTestWaveform();
      final h = soloud.play(sound);
      await soloud.stop(h);
      expect(soloud.findAudioSourceByHandle(h), isNull);
    });

    test('finds nothing once the source is disposed', () async {
      final sound = await loadTestWaveform();
      final h = soloud.play(sound);
      await soloud.disposeSource(sound);
      expect(soloud.findAudioSourceByHandle(h), isNull);
    });

    test('finds nothing for a handle of a previous engine session', () async {
      final sound = await loadTestWaveform();
      final h = soloud.play(sound);
      soloud.deinit();
      await initTestEngine(globalVolume: 0.05);
      expect(soloud.findAudioSourceByHandle(h), isNull);
    });
  });

  group('playSource', () {
    /// Where playSource reads a generated 1 s WAV from: a file on native, a
    /// data: URL in the browser (which cannot load files).
    Future<AudioSource> playGenerated({
      Duration duration = const Duration(seconds: 1),
      double volume = 1,
      double pan = 0,
      bool paused = false,
      bool looping = false,
    }) async {
      final bytes = wavBytes(duration: duration);
      if (kIsWeb) {
        return soloud.playSource(
          url: 'data:audio/wav;base64,${base64Encode(bytes)}',
          volume: volume,
          pan: pan,
          paused: paused,
          looping: looping,
        );
      }
      return soloud.playSource(
        file: writeTempWav(bytes),
        volume: volume,
        pan: pan,
        paused: paused,
        looping: looping,
      );
    }

    test('loads, plays and registers the source', () async {
      final sound = await playGenerated();
      expect(soloud.activeSounds, contains(sound));
      expect(sound.handles, hasLength(1));
      final h = sound.handles.single;
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.findAudioSourceByHandle(h), same(sound));
      expect(soloud.getPause(h), isFalse);
    });

    test('applies volume, pan, paused and looping', () async {
      final sound = await playGenerated(
        volume: 0.3,
        pan: -0.5,
        paused: true,
        looping: true,
      );
      final h = sound.handles.single;
      expect(soloud.getVolume(h), closeTo(0.3, 1e-6));
      expect(soloud.getPan(h), closeTo(-0.5, 1e-6));
      expect(soloud.getPause(h), isTrue);
      expect(soloud.getLooping(h), isTrue);
    });

    test('disposes the source when it ends', () async {
      final sound = await playGenerated(
        duration: const Duration(milliseconds: 150),
      );
      expect(
        await waitUntil(() => !soloud.activeSounds.contains(sound)),
        isTrue,
      );
    });

    test('a missing file throws and registers nothing', () async {
      final before = soloud.activeSounds.length;
      await expectLater(
        soloud.playSource(file: missingFilePath()),
        throwsA(isA<SoLoudException>()),
      );
      expect(soloud.activeSounds, hasLength(before));
    }, skip: kIsWeb ? 'the web cannot load files' : null);
  });

  group('audioDeviceStartFailures', () {
    test('is a broadcast stream, silent while the device works', () async {
      expect(soloud.audioDeviceStartFailures.isBroadcast, isTrue);
      final failures = <AudioDeviceStartFailure>[];
      final sub = soloud.audioDeviceStartFailures.listen(failures.add);
      addTearDown(sub.cancel);
      soloud.play(await loadTestWaveform());
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(failures, isEmpty);
    });

    test('republishes the native start-failure notification', () async {
      final failures = <AudioDeviceStartFailure>[];
      final sub = soloud.audioDeviceStartFailures.listen(failures.add);
      addTearDown(sub.cancel);
      // What the native lifecycle scheduler sends when it cannot start the
      // device.
      SoLoudController().soLoudFFI.stateChangedController.add(
        PlayerStateNotification.audioDeviceStartFailed,
      );
      expect(await waitUntil(() => failures.isNotEmpty), isTrue);
      expect(failures, [AudioDeviceStartFailure.deviceUnavailable]);
    });

    test('other notifications are not failures', () async {
      final failures = <AudioDeviceStartFailure>[];
      final sub = soloud.audioDeviceStartFailures.listen(failures.add);
      addTearDown(sub.cancel);
      SoLoudController().soLoudFFI.stateChangedController
        ..add(PlayerStateNotification.started)
        ..add(PlayerStateNotification.stopped);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(failures, isEmpty);
    });
  });
}

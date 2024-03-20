import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/soloud_controller.dart';
import 'package:logging/logging.dart';

/// An end-to-end test.
///
/// Run this with `flutter run tests/tests.dart`.
void main() async {
  // Make sure we can see logs from the engine, even in release mode.
  // ignore: avoid_print
  final errorsBuffer = StringBuffer();
  Logger.root.onRecord.listen((record) {
    debugPrint(record.toString(), wrapWidth: 80);
    if (record.level >= Level.WARNING) {
      // Make sure the warnings are visible.
      stderr.writeln('TEST error: $record');
      errorsBuffer.writeln('- $record');
      // Set exit code but keep running to see all logs.
      exitCode = 1;
    }
  });
  Logger.root.level = Level.ALL;

  WidgetsFlutterBinding.ensureInitialized();

  await runZonedGuarded(
    () async => test6(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  await runZonedGuarded(
    () async => test5(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  await runZonedGuarded(
    () async => test4(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  await runZonedGuarded(
    () async => test3(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  await runZonedGuarded(
    () async => test1(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  await runZonedGuarded(
    () async => test2(),
    (error, stack) {
      stderr.writeln('TEST error: $error\nstack: $stack');
      exitCode = 1;
    },
  );

  stdout.write('\n\n\n---\n\n\n');

  if (exitCode != 0) {
    // Since we're running this inside `flutter run`, the exit code
    // will be overridden to 0 by the Flutter tool.
    // The following is making sure that the errors are noticed.
    stderr
      ..writeln('===== TESTS FAILED with the following error(s) =====')
      ..writeln()
      ..writeln(errorsBuffer.toString())
      ..writeln()
      ..writeln('See logs above for details.')
      ..writeln();
  } else {
    stdout
      ..writeln('===== TESTS PASSED! =====')
      ..writeln();
  }

  // Cleanly close the app.
  await SystemChannels.platform.invokeMethod('SystemNavigator.pop');
}

String output = '';
AudioSource? currentSound;

Future<void> delay(int ms) async {
  await Future.delayed(Duration(milliseconds: ms), () {});
}

/// Test setMaxActiveVoiceCount, setProtectedVoice and getProtectedVoice
Future<void> test6() async {
  await initialize();

  SoLoud.instance.setMaxActiveVoiceCount(3);
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 3,
    "setMaxActiveVoiceCount() didn't worked correctly",
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    await SoLoud.instance.play(explosion);
    await delay(100);
  }

  /// play 1 protected [song]
  final songHandle = await SoLoud.instance.play(song);
  SoLoud.instance.setProtectVoice(songHandle.id, true);
  assert(
    SoLoud.instance.getProtectVoice(songHandle.id),
    "setProtectVoice() didn't worked correctly",
  );

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    await SoLoud.instance.play(explosion);
    await delay(100);
  }

  await delay(1000);

  assert(
    SoLoud.instance.getIsValidVoiceHandle(songHandle) &&
        SoLoud.instance.getActiveVoiceCount() == 3,
    'The protected song has been stopped!',
  );

  await dispose();
}

/// Test allInstancesFinished stream
Future<void> test5() async {
  final log = Logger('test5');
  await initialize();

  await SoLoud.instance.disposeAllSound();
  assert(
    SoLoud.instance.activeSounds.isEmpty,
    'Active sounds even after disposeAllSound()',
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  // Set up unloading.
  var explosionDisposed = false;
  var songDisposed = false;
  unawaited(
    explosion.allInstancesFinished.first.then((_) async {
      log.info('All instances of explosion finished.');
      await SoLoud.instance.disposeSound(explosion);
      explosionDisposed = true;
    }),
  );
  unawaited(
    song.allInstancesFinished.first.then((_) async {
      log.info('All instances of song finished.');
      await SoLoud.instance.disposeSound(song);
      songDisposed = true;
    }),
  );

  await SoLoud.instance.play(explosion, volume: 0.2);
  final songHandle = await SoLoud.instance.play(song, volume: 0.6);
  await Future<void>.delayed(const Duration(milliseconds: 500));
  await SoLoud.instance.play(explosion, volume: 0.3);

  // Let the second explosion play for its full duration.
  await Future<void>.delayed(const Duration(milliseconds: 4000));

  await SoLoud.instance.stop(songHandle);
  await Future<void>.delayed(const Duration(milliseconds: 1000));

  assert(explosionDisposed, "Explosion sound wasn't disposed.");
  assert(songDisposed, "Song sound wasn't disposed.");

  await dispose();
}

/// Test synchronous `deinit()`
Future<void> test4() async {
  /// test init-deinit looping with a short decreasing time
  for (var t = 500; t >= 0; t -= 50) {
    /// Initialize the player
    var error = '';
    await SoLoud.instance.initialize().then(
      (_) {},
      onError: (Object e) {
        e = 'TEST FAILED delay: $t. Player starting error: $e';
        error = e.toString();
      },
    );

    assert(error.isEmpty, error);

    final before = SoLoudController().soLoudFFI.isInited();

    /// wait for [t] ms and deinit()
    await Future.delayed(Duration(milliseconds: t), () {});
    SoLoud.instance.deinit();
    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      before == !after,
      'TEST FAILED delay: $t. The player has not been '
      'inited or deinited correctly!',
    );

    print('------------- delay $t passed\n');
  }

  /// Try init-play-deinit and again init-play without disposing the sound
  await SoLoud.instance.initialize();

  await loadAsset();
  await SoLoud.instance.play(currentSound!);
  await delay(100);
  await SoLoud.instance.play(currentSound!);
  await delay(100);
  await SoLoud.instance.play(currentSound!);

  await delay(2000);

  SoLoud.instance.deinit();

  /// Initialize again and check if the sound has been
  /// disposed correctly by `deinit()`
  await SoLoud.instance.initialize();
  assert(
    SoLoudController()
            .soLoudFFI
            .getIsValidVoiceHandle(currentSound!.handles.first) ==
        false,
    'getIsValidVoiceHandle(): sound not disposed by the engine',
  );
  assert(
    SoLoudController()
            .soLoudFFI
            .countAudioSource(currentSound!.soundHash.hash) ==
        0,
    'getCountAudioSource(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.getActiveVoiceCount() == 0,
    'getActiveVoiceCount(): sound not disposed by the engine',
  );
  SoLoud.instance.deinit();
}

/// Test waveform
///
Future<void> test3() async {
  await initialize();
  final notes = await SoLoudTools.createNotes(
    octave: 1,
  );
  assert(
    notes.length == 12,
    'SoloudTools.initSounds() failed!',
  );

  for (var i = 2; i < 10; i++) {
    const volume = 0.2;
    final d = (sin(i / 6.28) * 400).toInt();
    await SoLoud.instance.play(notes[7], volume: volume);
    await delay(500 - d);
    await SoLoud.instance.stop(notes[7].handles.first);

    await SoLoud.instance.play(notes[10], volume: volume);
    await delay(550 - d);
    await SoLoud.instance.stop(notes[10].handles.first);

    await SoLoud.instance.play(notes[7], volume: volume);
    await delay(500 - d);
    await SoLoud.instance.stop(notes[7].handles.first);

    await SoLoud.instance.play(notes[0], volume: volume);
    await delay(500 - d);
    await SoLoud.instance.stop(notes[0].handles.first);

    await SoLoud.instance.play(notes[4], volume: volume);
    await delay(800 - d);
    await SoLoud.instance.stop(notes[4].handles.first);

    await delay(300);
  }

  await dispose();
}

/// Test play, pause, seek, position
///
Future<void> test2() async {
  /// Start audio isolate
  await initialize();

  /// Load sample
  await loadAsset();

  /// pause, seek test
  {
    await SoLoud.instance.play(currentSound!);
    final length = SoLoud.instance.getLength(currentSound!);
    assert(
      length.inMilliseconds == 3840,
      'getLength() failed: ${length.inMilliseconds}!',
    );
    await delay(1000);
    SoLoud.instance.pauseSwitch(currentSound!.handles.first);
    final paused = SoLoud.instance.getPause(currentSound!.handles.first);
    assert(paused, 'pauseSwitch() failed!');

    /// seek
    const wantedPosition = Duration(seconds: 2);
    SoLoud.instance.seek(currentSound!.handles.first, wantedPosition);
    final position = SoLoud.instance.getPosition(currentSound!.handles.first);
    assert(position == wantedPosition, 'getPosition() failed!');
  }

  await dispose();
}

/// Test start/stop isolate, load, play and events from sound
///
Future<void> test1() async {
  /// Start audio isolate
  await initialize();

  /// Load sample
  await loadAsset();

  /// Play sample
  {
    await SoLoud.instance.play(currentSound!);
    assert(
      currentSound!.soundHash.isValid && currentSound!.handles.length == 1,
      'play() failed!',
    );

    /// Wait for the sample to finish and see in log:
    /// "@@@@@@@@@@@ SOUND EVENT: SoundEvent.soundDisposed .*"
    /// 3798ms explosion.mp3 sample duration
    await delay(4500);
    assert(
      output == 'SoundEvent.handleIsNoMoreValid',
      'Sound end playback event not triggered!',
    );
  }

  /// Play 4 sample
  {
    await SoLoud.instance.play(currentSound!);
    await SoLoud.instance.play(currentSound!);
    await SoLoud.instance.play(currentSound!);
    await SoLoud.instance.play(currentSound!);
    assert(
      currentSound!.handles.length == 4,
      'loadFromAssets() failed!',
    );

    /// Wait for the sample to finish and see in log:
    /// "SoundEvent.handleIsNoMoreValid .* has [3-2-1-0] active handles"
    /// 3798ms explosion.mp3 sample duration
    await delay(4500);
    assert(
      currentSound!.handles.isEmpty,
      'Play 4 sample handles failed!',
    );
  }

  /// Stop isolate
  {
    /// Stop player and see in log:
    /// "@@@@@@@@@@@ SOUND EVENT: SoundEvent.soundDisposed .*"
    await dispose();
    assert(
      output == 'SoundEvent.soundDisposed',
      'Sound end playback event not triggered!',
    );
  }
}

/// Common methods
Future<void> initialize() async {
  await SoLoud.instance.initialize();
}

Future<void> dispose() async {
  final ret = await SoLoud.instance.shutdown();
  assert(ret, 'dispose() failed!');
}

Future<void> loadAsset() async {
  if (currentSound != null) {
    await SoLoud.instance.disposeSound(currentSound!);
  }
  currentSound = await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  currentSound!.soundEvents.listen((event) {
    if (event.event == SoundEventType.handleIsNoMoreValid) {
      output = 'SoundEvent.handleIsNoMoreValid';
    }
    if (event.event == SoundEventType.soundDisposed) {
      output = 'SoundEvent.soundDisposed';
    }
  });
}

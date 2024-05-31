import 'dart:async';
import 'dart:io';

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
      // Exception for deiniting.
      if (record.error is SoLoudInitializationStoppedByDeinitException) {
        return;
      }

      // Make sure the warnings are visible.
      stderr.writeln('TEST error (${record.level} log): $record');
      errorsBuffer.writeln('- $record');
      // Set exit code but keep running to see all logs.
      exitCode = 1;
    }
  });
  Logger.root.level = Level.ALL;

  WidgetsFlutterBinding.ensureInitialized();

  var tests = <Future<void> Function()>[
    testProtectVoice,
    testAllInstancesFinished,
    testCreateNotes,
    testPlaySeekPause,
    testHandles,
    loopingTests,
  ];
  for (final f in tests) {
    await runZonedGuarded(
      () async => f(),
      (error, stack) => printError,
    );
  }

  tests = <Future<void> Function()>[
    testSynchronousDeinit,
    testAsynchronousDeinit,
  ];
  for (final f in tests) {
    await runZonedGuarded(
      () async => f(),
      (error, stack) {
        if (error is SoLoudInitializationStoppedByDeinitException) {
          // This is to be expected in this test.
          return;
        }
        printError(error, stack);
      },
    );
  }

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
    debugPrint('===== TESTS PASSED! =====');
    stdout
      ..writeln('===== TESTS PASSED! =====')
      ..writeln();
  }

  // Cleanly close the app.
  await SystemChannels.platform.invokeMethod('SystemNavigator.pop');
}

String output = '';
AudioSource? currentSound;

/// Test setMaxActiveVoiceCount, setProtectedVoice and getProtectedVoice
Future<void> testProtectVoice() async {
  await initialize();
  final defaultVoiceCount = SoLoud.instance.getMaxActiveVoiceCount();

  SoLoud.instance.setMaxActiveVoiceCount(3);
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 3,
    "setMaxActiveVoiceCount() didn't work properly",
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
  SoLoud.instance.setProtectVoice(songHandle, true);
  assert(
    SoLoud.instance.getProtectVoice(songHandle),
    "setProtectVoice() didn't work properly",
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

  deinit();

  /// Afer disposing the player and re-initializing, max active voices
  /// should be reset to 16
  await initialize();
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == defaultVoiceCount,
    'Max active voices are not reset to the default value after reinit!',
  );
  deinit();
}

/// Test allInstancesFinished stream
Future<void> testAllInstancesFinished() async {
  final log = Logger('testAllInstancesFinished');
  await initialize();

  await SoLoud.instance.disposeAllSources();
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
      await SoLoud.instance.disposeSource(explosion);
      explosionDisposed = true;
    }),
  );
  unawaited(
    song.allInstancesFinished.first.then((_) async {
      log.info('All instances of song finished.');
      await SoLoud.instance.disposeSource(song);
      songDisposed = true;
    }),
  );

  await SoLoud.instance.play(explosion, volume: 0.2);
  final songHandle = await SoLoud.instance.play(song, volume: 0.6);
  await delay(500);
  await SoLoud.instance.play(explosion, volume: 0.3);

  // Let the second explosion play for its full duration.
  await delay(4000);

  await SoLoud.instance.stop(songHandle);
  await delay(1000);

  assert(explosionDisposed, "Explosion sound wasn't disposed.");
  assert(songDisposed, "Song sound wasn't disposed.");

  deinit();
}

/// Test asynchronous `init()`-`deinit()`
Future<void> testAsynchronousDeinit() async {
  final log = Logger('testAsynchronousDeinit');

  /// test asynchronous init-deinit looping with a short decreasing time
  for (var t = 100; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    unawaited(
      SoLoud.instance.init().then(
        (_) {},
        onError: (Object e) {
          if (e is SoLoudInitializationStoppedByDeinitException) {
            // This is to be expected.
            log.info('$e');
            return;
          }
          e = 'TEST FAILED delay: $t. Player starting error: $e';
          error = e.toString();
        },
      ),
    );

    assert(error.isEmpty, error);

    /// wait for [t] ms and deinit()
    await delay(t);
    SoLoud.instance.deinit();
    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      after == false,
      'TEST FAILED delay: $t. The player has not been deinited correctly!',
    );

    stderr.writeln('------------- awaited init delay $t passed\n');
  }
}

/// Test synchronous `init()`-`deinit()`
Future<void> testSynchronousDeinit() async {
  final log = Logger('testSynchronousDeinit');

  /// test synchronous init-deinit looping with a short decreasing time
  /// waiting for `initialize()` to finish
  for (var t = 100; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    await SoLoud.instance.init().then(
      (_) {},
      onError: (Object e) {
        if (e is SoLoudInitializationStoppedByDeinitException) {
          // This is to be expected.
          log.info('$e');
          return;
        }
        e = 'TEST FAILED delay: $t. Player starting error: $e';
        error = e.toString();
      },
    );
    assert(
      error.isEmpty,
      'ASSERT FAILED delay: $t. The player has not been '
      'inited correctly!',
    );

    SoLoud.instance.deinit();

    assert(
      !SoLoud.instance.isInitialized ||
          !SoLoudController().soLoudFFI.isInited(),
      'ASSERT FAILED delay: $t. The player has not been '
      'inited or deinited correctly!',
    );

    stderr.writeln('------------- awaited init #$t passed\n');
  }

  /// Try init-play-deinit and again init-play without disposing the sound
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);

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
  await SoLoud.instance.init();
  assert(
    SoLoudController()
            .soLoudFFI
            .getIsValidVoiceHandle(currentSound!.handles.first) ==
        false,
    'getIsValidVoiceHandle(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.countAudioSource(currentSound!.soundHash) == 0,
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
Future<void> testCreateNotes() async {
  await initialize();

  final notes0 = await SoLoudTools.createNotes(
    octave: 0,
  );
  final notes1 = await SoLoudTools.createNotes(
    octave: 1,
  );
  final notes2 = await SoLoudTools.createNotes(
    octave: 2,
  );
  assert(
    notes0.length == 12 && notes1.length == 12 && notes2.length == 12,
    'SoLoudTools.createNotes() failed!',
  );

  await SoLoud.instance.play(notes1[5]);
  await SoLoud.instance.play(notes2[0]);
  await delay(350);
  await SoLoud.instance.stop(notes1[5].handles.first);
  await SoLoud.instance.stop(notes2[0].handles.first);

  await SoLoud.instance.play(notes1[6]);
  await SoLoud.instance.play(notes2[1]);
  await delay(350);
  await SoLoud.instance.stop(notes1[6].handles.first);
  await SoLoud.instance.stop(notes2[1].handles.first);

  await SoLoud.instance.play(notes1[4]);
  await SoLoud.instance.play(notes1[11]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes1[11].handles.first);

  await SoLoud.instance.play(notes1[4]);
  await SoLoud.instance.play(notes0[9]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes0[9].handles.first);

  await SoLoud.instance.play(notes1[8]);
  await SoLoud.instance.play(notes1[1]);
  await delay(1500);
  await SoLoud.instance.stop(notes1[8].handles.first);
  await SoLoud.instance.stop(notes1[1].handles.first);

  deinit();
}

/// Test play, pause, seek, position
///
Future<void> testPlaySeekPause() async {
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

  deinit();
}

/// Test instancing playing handles and their disposal
Future<void> testHandles() async {
  /// Start audio isolate
  await initialize();

  /// Load sample
  await loadAsset();

  /// Play sample
  await SoLoud.instance.play(currentSound!);
  assert(
    currentSound!.soundHash.isValid && currentSound!.handles.length == 1,
    'play() failed!',
  );

  /// 3798ms explosion.mp3 sample duration
  await delay(4500);
  assert(
    output == 'SoundEvent.handleIsNoMoreValid',
    'Sound end playback event not triggered!',
  );

  /// Play 4 sample
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

  deinit();
}

/// Test looping state and `loopingStartAt`
Future<void> loopingTests() async {
  await initialize();

  await loadAsset();

  await SoLoud.instance.play(
    currentSound!,
    looping: true,
    loopingStartAt: const Duration(seconds: 1),
  );
  assert(
    SoLoud.instance.getLooping(currentSound!.handles.first),
    'looping failed!',
  );

  /// Wait for the first loop to start at 1s
  await delay(4100);
  assert(
    SoLoud.instance.getLoopPoint(currentSound!.handles.first) ==
            const Duration(seconds: 1) &&
        SoLoud.instance.getPosition(currentSound!.handles.first) >
            const Duration(seconds: 1),
    'looping start failed!',
  );

  deinit();
}

/// Common methods
Future<void> initialize() async {
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);
}

void deinit() {
  SoLoud.instance.deinit();
}

Future<void> delay(int ms) async {
  await Future.delayed(Duration(milliseconds: ms), () {});
}

Future<void> loadAsset() async {
  if (currentSound != null) {
    await SoLoud.instance.disposeSource(currentSound!);
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

void printError(Object error, StackTrace stack) {
  stderr.writeln('TEST error: $error\nstack: $stack');
  exitCode = 1;
}

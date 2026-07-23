import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// One-shot accuracy test for [SoLoud.playClocked].
///
/// The engine is initialized with the buffer size given by the `bufferSize`
/// constant below (4096 is ~93 ms and 8192 is ~186 ms at 44100 Hz). A
/// metronome is then run with tick delays of 50, 100 and 200 ms using
/// [SoLoud.playClocked] and the master mixer output is captured as
/// `pcmF32le` (mono, 44100 Hz), one raw file per delay, saved into the
/// system temp directory.
///
/// The tick onsets can then be measured in the captured files and compared
/// with the requested delays.
///
/// Run from the `example` directory with:
/// ```bash
/// flutter run -d macos -t lib/metronome/clocked_accuracy_test.dart
/// ```
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  const sampleRate = 44100;

  /// Engine buffer size in samples. One output buffer lasts
  /// `bufferSize / sampleRate` seconds (~93 ms for 4096, ~186 ms for 8192).
  const bufferSize = 4096;

  await SoLoud.instance.init(
    bufferSize: bufferSize,
    channels: Channels.mono,
  );

  final tick = await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');

  const delaysMs = [50, 100, 200];
  const ticksPerRun = 10;

  for (final delayMs in delaysMs) {
    // Each run is a new scheduling session with its own time base: reset
    // the clocked-play anchor so the run re-anchors to its own clock.
    SoLoud.instance.resetStreamTime();

    final file = File(
      '${Directory.systemTemp.path}/clocked_${bufferSize}_$delayMs'
      'ms.f32le',
    );
    final raf = file.openSync(mode: FileMode.writeOnly);
    var totalBytes = 0;

    // ignore: experimental_member_use
    final stream = SoLoud.instance.startMixerOutputStream(
      // ignore: avoid_redundant_argument_values
      format: MixerOutputFormat.pcmF32le,
      sampleRate: sampleRate,
      channels: 1,
    );

    final subscription = stream.listen(
      (chunk) {
        raf.writeFromSync(chunk);
        totalBytes += chunk.length;
      },
      // ignore: avoid_print
      onError: (Object e) => print('capture error: $e'),
    );

    // Let the capture stream start cleanly before scheduling ticks.
    await Future<void>.delayed(const Duration(milliseconds: 300));

    // Metronome: schedule the ticks with playClocked using an accumulated
    // ideal "physics time".
    var physicsTime = Duration.zero;
    final tickDelay = Duration(milliseconds: delayMs);
    final done = Completer<void>();
    var count = 0;
    Timer.periodic(tickDelay, (timer) {
      physicsTime += tickDelay;
      SoLoud.instance.playClocked(tick, physicsTime);
      count++;
      if (count >= ticksPerRun) {
        timer.cancel();
        done.complete();
      }
    });
    await done.future;

    // Wait for the last tick to be played and captured.
    await Future<void>.delayed(const Duration(milliseconds: 600));

    // ignore: experimental_member_use
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();
    await raf.close();

    final seconds = totalBytes / 4 / sampleRate;
    // ignore: avoid_print
    print(
      'CLOCKED_TEST_FILE delay=${delayMs}ms path=${file.path} '
      'bytes=$totalBytes (~${seconds.toStringAsFixed(2)}s)',
    );

    // Small pause between runs.
    await Future<void>.delayed(const Duration(milliseconds: 300));
  }

  SoLoud.instance.deinit();

  // ignore: avoid_print
  print('CLOCKED_TEST_DONE');
  exit(0);
}

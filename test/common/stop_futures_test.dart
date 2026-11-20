// stop(), scheduleStop() and schedulePause() with the real engine and a
// generated sound. stop() futures must complete also for a handle that has
// already ended, without severe logs.
@Tags(['device'])
library;

import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:logging/logging.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests();

  final severe = <LogRecord>[];
  late StreamSubscription<LogRecord> logs;
  late AudioSource sound;

  setUp(() async {
    severe.clear();
    logs = Logger.root.onRecord.listen((r) {
      if (r.level >= Level.SEVERE) severe.add(r);
    });
    sound = await loadSine(duration: const Duration(seconds: 3));
  });

  tearDown(() async {
    await logs.cancel();
    expect(
      severe.map((r) => '[${r.level}] ${r.message}'),
      isEmpty,
      reason: 'severe logs',
    );
  });

  const timeout = Duration(seconds: 2);

  test('stop right after play completes', () async {
    final handle = SoLoud.instance.play(sound);
    await SoLoud.instance.stop(handle).timeout(timeout);
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isFalse);
  });

  test('scheduleStop ends the voice, and a later stop completes', () async {
    final handle = SoLoud.instance.play(sound);
    SoLoud.instance.scheduleStop(handle, const Duration(milliseconds: 200));
    expect(
      await waitUntil(() => !SoLoud.instance.getIsValidVoiceHandle(handle)),
      isTrue,
      reason: 'the scheduled stop did not end the voice',
    );
    await SoLoud.instance.stop(handle).timeout(timeout);
  });

  test('schedulePause pauses only after its delay', () async {
    final handle = SoLoud.instance.play(sound);
    final watch = Stopwatch()..start();
    SoLoud.instance.schedulePause(handle, const Duration(milliseconds: 300));
    expect(SoLoud.instance.getPause(handle), isFalse);
    expect(await waitUntil(() => SoLoud.instance.getPause(handle)), isTrue);
    // Generous: the engine applies it on a mix buffer boundary.
    expect(watch.elapsedMilliseconds, greaterThanOrEqualTo(200));
  });

  test('a manual stop before a scheduled stop wins', () async {
    final handle = SoLoud.instance.play(sound);
    SoLoud.instance.scheduleStop(handle, const Duration(seconds: 5));
    await Future<void>.delayed(const Duration(milliseconds: 50));
    await SoLoud.instance.stop(handle).timeout(timeout);
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isFalse);
  });
}

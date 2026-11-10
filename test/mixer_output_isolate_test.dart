// Mixer output capture from worker isolates, with the real engine. A capture
// delivers its packets to the isolate whose start was admitted. That isolate
// can end while the capture runs (Isolate.run returning, Isolate.exit,
// Isolate.kill), and another isolate's start, refused or not yet admitted,
// must not take the running capture's packets over.
@TestOn('vm')
library;

import 'dart:io';
import 'dart:isolate';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/enums.dart' show PlayerErrors;
import 'package:flutter_test/flutter_test.dart';

String _startAndReturn() {
  SoLoudIsolate.instance.startMixerOutputStream(chunkPCMFrames: 2048);
  return 'started';
}

void _startUntilTerminated(List<Object> args) {
  final reply = args[0] as SendPort;
  final exitNow = args[1] as bool;
  SoLoudIsolate.instance.startMixerOutputStream(chunkPCMFrames: 2048);
  if (exitNow) Isolate.exit(reply, 'started');
  reply.send('started');
  // Keeps the isolate alive until it is killed.
  ReceivePort().listen((_) {});
}

String _startInWorker() {
  try {
    SoLoudIsolate.instance.startMixerOutputStream(chunkPCMFrames: 2048);
    return 'started';
  } on SoLoudCppException {
    return 'refused';
  }
}

/// The first step of MixerOutputStreamManager.start(), in a worker that then
/// returns without starting.
String _prepareAndReturn() {
  SoLoudIsolate.instance.bindings.registerMixerOutputCallback();
  return 'prepared';
}

Future<void> _waitUntil(bool Function() done) async {
  for (var i = 0; i < 150 && !done(); i++) {
    await Future<void>.delayed(const Duration(milliseconds: 20));
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final temp = Directory.systemTemp.createTempSync('soloud_capture_isolate');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );
  tearDownAll(() => temp.deleteSync(recursive: true));

  test('a worker returns with its capture running', () async {
    final soloud = SoLoud.instance;
    await soloud.init();
    expect(await Isolate.run(_startAndReturn), 'started');
    // The capture keeps notifying chunks after the worker has gone.
    await Future<void>.delayed(const Duration(milliseconds: 500));
    soloud.deinit();
  });

  for (final exitNow in [true, false]) {
    test(
      exitNow
          ? 'a worker exits with its capture running'
          : 'a worker is killed with its capture running',
      () async {
        final soloud = SoLoud.instance;
        await soloud.init();
        final ready = ReceivePort();
        final exited = ReceivePort();
        final worker = await Isolate.spawn(_startUntilTerminated, <Object>[
          ready.sendPort,
          exitNow,
        ], onExit: exited.sendPort);
        expect(await ready.first, 'started');
        if (!exitNow) worker.kill(priority: Isolate.immediate);
        await exited.first;
        await Future<void>.delayed(const Duration(milliseconds: 500));
        ready.close();
        exited.close();
        soloud.deinit();
      },
    );
  }

  test("an ended worker's capture gives way to the next start", () async {
    final soloud = SoLoud.instance;
    await soloud.init();
    expect(await Isolate.run(_startAndReturn), 'started');
    var chunks = 0;
    final subscription = soloud
        .startMixerOutputStream(chunkPCMFrames: 2048)
        .listen((_) => chunks++);
    await _waitUntil(() => chunks > 0);
    expect(chunks, greaterThan(0), reason: 'the new capture delivers chunks');
    soloud.stopMixerOutputStream();
    await subscription.cancel();
    soloud.deinit();
  });

  test('a refused worker start leaves the running capture alone', () async {
    final soloud = SoLoud.instance;
    await soloud.init();
    var chunks = 0;
    final subscription = soloud
        .startMixerOutputStream(chunkPCMFrames: 2048)
        .listen((_) => chunks++);
    await _waitUntil(() => chunks > 0);
    expect(chunks, greaterThan(0), reason: 'the main capture delivers chunks');

    expect(await Isolate.run(_startInWorker), 'refused');
    final before = chunks;
    await _waitUntil(() => chunks > before);
    expect(
      chunks,
      greaterThan(before),
      reason: 'the main stream keeps its chunks after the refusal',
    );
    soloud.stopMixerOutputStream();
    await subscription.cancel();
    soloud.deinit();
  });

  test('a worker that prepared but never started takes nothing over', () async {
    final soloud = SoLoud.instance;
    await soloud.init();
    final bindings = SoLoudIsolate.instance.bindings;
    var chunks = 0;
    final subscription = bindings.mixerOutputChunkEvents.listen(
      (_) => chunks++,
    );
    // Two managers starting at once: both prepare, the worker gives up and
    // returns, the main isolate's start is admitted.
    bindings.registerMixerOutputCallback();
    expect(await Isolate.run(_prepareAndReturn), 'prepared');
    expect(
      bindings.startMixerOutputCapture(
        MixerOutputFormat.pcmF32le,
        -1,
        -1,
        1024 * 1024,
        4096,
        2048,
      ),
      PlayerErrors.noError,
    );
    await _waitUntil(() => chunks > 0);
    expect(chunks, greaterThan(0), reason: 'the admitted start gets chunks');
    bindings.stopMixerOutputCapture();
    await subscription.cancel();
    soloud.deinit();
  });
}

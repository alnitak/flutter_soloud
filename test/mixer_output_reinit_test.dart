// deinit() ends the engine, and the mixer output capture with it. The stream
// of the isolate that deinitializes the engine closes then, with the audio
// its capture still owed it. A worker isolate's stream, which deinit() cannot
// reach, closes on that worker's next start, and that start captures again.
@TestOn('vm')
library;

import 'dart:async';
import 'dart:io';
import 'dart:isolate';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

Future<void> _waitUntil(bool Function() done) async {
  for (var i = 0; i < 150 && !done(); i++) {
    await Future<void>.delayed(const Duration(milliseconds: 20));
  }
}

/// A worker that keeps its stream across the main isolate's deinit() and
/// init(), and starts, reports and stops on command.
void _captureWorker(SendPort reply) {
  final commands = ReceivePort();
  reply.send(commands.sendPort);
  var chunks = 0;
  var closed = 0;
  commands.listen((Object? command) {
    switch (command) {
      case 'start':
        SoLoudIsolate.instance
            .startMixerOutputStream(chunkPCMFrames: 2048)
            .listen((_) => chunks++, onDone: () => closed++);
        chunks = 0;
        reply.send('started');
      case 'report':
        reply.send(<Object>[
          SoLoudIsolate.instance.isMixerOutputStreamRunning,
          chunks,
          closed,
        ]);
      case 'stop':
        SoLoudIsolate.instance.stopMixerOutputStream();
        commands.close();
        reply.send('stopped');
    }
  });
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final temp = Directory.systemTemp.createTempSync('soloud_capture_reinit');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );
  tearDownAll(() => temp.deleteSync(recursive: true));

  test(
    'deinit ends the stream, and a start after init captures again',
    () async {
      final soloud = SoLoud.instance;
      await soloud.init();
      var chunks = 0;
      var bytes = 0;
      int? deliveredWhenClosed;
      final first = soloud.startMixerOutputStream(chunkPCMFrames: 2048).listen((
        chunk,
      ) {
        chunks++;
        bytes += chunk.length;
      }, onDone: () => deliveredWhenClosed = bytes);
      await _waitUntil(() => chunks > 0);
      final chunkBytes = bytes ~/ chunks;
      bytes = 0;
      // Busy, this isolate leaves the next chunks' notifications queued.
      final busy = Stopwatch()..start();
      while (busy.elapsedMilliseconds < 300) {}
      soloud.deinit();
      await Future<void>.delayed(Duration.zero);
      expect(deliveredWhenClosed, isNotNull, reason: 'deinit ends the stream');
      expect(
        deliveredWhenClosed,
        greaterThanOrEqualTo(2 * chunkBytes),
        reason: 'with the chunks still queued for it',
      );

      await soloud.init();
      chunks = 0;
      final second = soloud
          .startMixerOutputStream(chunkPCMFrames: 2048)
          .listen((_) => chunks++);
      expect(
        soloud.isMixerOutputStreamRunning,
        isTrue,
        reason: 'a new capture runs behind the new stream',
      );
      await _waitUntil(() => chunks > 0);
      expect(chunks, greaterThan(0), reason: 'and delivers chunks');
      soloud.stopMixerOutputStream();
      await first.cancel();
      await second.cancel();
      soloud.deinit();
    },
  );

  test(
    "a worker's stream closes on its next start, which captures again",
    () async {
      final soloud = SoLoud.instance;
      await soloud.init();
      final replies = ReceivePort();
      final next = StreamIterator<Object?>(replies);
      await Isolate.spawn(_captureWorker, replies.sendPort);
      await next.moveNext();
      final commands = next.current! as SendPort;
      Future<Object?> ask(String command) async {
        commands.send(command);
        await next.moveNext();
        return next.current;
      }

      expect(await ask('start'), 'started');
      soloud.deinit();
      await soloud.init();
      expect(await ask('start'), 'started');
      var report = const <Object?>[];
      for (var i = 0; i < 150; i++) {
        report = (await ask('report'))! as List<Object?>;
        if ((report[1]! as int) > 0) break;
        await Future<void>.delayed(const Duration(milliseconds: 20));
      }
      expect(report[0], isTrue, reason: 'the worker runs a new capture');
      expect(report[1], greaterThan(0), reason: 'which delivers chunks');
      expect(report[2], 1, reason: 'and its old stream was closed');
      expect(await ask('stop'), 'stopped');
      await next.cancel();
      soloud.deinit();
    },
  );
}

// Stopping a mixer output stream, with the real engine. Chunks the capture
// has notified to this isolate but that it has not handled yet when the
// stream stops belong to the stream: they arrive before it closes, at their
// size, followed by the tail.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

Future<void> _waitUntil(bool Function() done) async {
  for (var i = 0; i < 150 && !done(); i++) {
    await Future<void>.delayed(const Duration(milliseconds: 20));
  }
}

int _bytes(List<Uint8List> chunks) =>
    chunks.fold(0, (sum, chunk) => sum + chunk.length);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  final temp = Directory.systemTemp.createTempSync('soloud_capture_stop');
  TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
      .setMockMethodCallHandler(
        const MethodChannel('plugins.flutter.io/path_provider'),
        (call) async => temp.path,
      );
  tearDownAll(() => temp.deleteSync(recursive: true));

  test('chunks queued when the stream stops are delivered', () async {
    final soloud = SoLoud.instance;
    await soloud.init();
    final chunks = <Uint8List>[];
    int? deliveredWhenClosed;
    final subscription = soloud
        .startMixerOutputStream(chunkPCMFrames: 2048)
        .listen(chunks.add, onDone: () => deliveredWhenClosed = _bytes(chunks));
    await _waitUntil(() => chunks.isNotEmpty);
    expect(chunks, isNotEmpty, reason: 'the capture delivers chunks');
    final chunkBytes = chunks.first.length;
    chunks.clear();

    // Busy, this isolate leaves the next chunks' notifications queued.
    final busy = Stopwatch()..start();
    while (busy.elapsedMilliseconds < 400) {}
    soloud.stopMixerOutputStream();
    await Future<void>.delayed(Duration.zero);

    expect(
      deliveredWhenClosed,
      greaterThanOrEqualTo(3 * chunkBytes),
      reason: 'about 400 ms of chunks were queued when the stream stopped',
    );
    expect(_bytes(chunks), deliveredWhenClosed, reason: 'nothing after');
    expect(
      chunks.take(chunks.length - 1).map((chunk) => chunk.length),
      everyElement(chunkBytes),
      reason: 'only the tail is shorter than a chunk',
    );
    await subscription.cancel();
    soloud.deinit();
  });
}

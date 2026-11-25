// Buffer stream callbacks with the real engine. The NativeCallable behind
// onBuffering must live as long as the sound (PR #445: it was dropped right
// after setBufferStream(), so the GC could free the trampoline native still
// called), and autoDispose buffer streams must be disposed when they end.
@TestOn('vm')
@Tags(['device'])
library;

import 'dart:async';
import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// A mono s16le sine at 24 kHz (the setBufferStream defaults).
Uint8List _pcm({double seconds = 1, int sampleRate = 24000}) {
  final count = (sampleRate * seconds).toInt();
  final bytes = ByteData(count * 2);
  for (var i = 0; i < count; i++) {
    final sample = (sin(2 * pi * 440 * i / sampleRate) * 16000).toInt();
    bytes.setInt16(i * 2, sample, Endian.little);
  }
  return bytes.buffer.asUint8List();
}

/// Plays a stream with no data, then feeds [pcm]: the stream buffers and
/// un-buffers, both reported through onBuffering. Returns the events seen
/// until the first un-buffering.
Future<List<bool>> _bufferAndUnbuffer(Uint8List pcm) async {
  final events = <bool>[];
  final unbuffered = Completer<void>();
  final stream = SoLoud.instance.setBufferStream(
    bufferingTimeNeeds: 0.3,
    onBuffering: (isBuffering, handle, time) {
      events.add(isBuffering);
      if (!isBuffering && !unbuffered.isCompleted) unbuffered.complete();
    },
  );
  SoLoud.instance.play(stream);
  await Future<void>.delayed(const Duration(milliseconds: 100));
  SoLoud.instance.addAudioDataStream(stream, pcm);
  await unbuffered.future.timeout(const Duration(seconds: 5));
  await SoLoud.instance.disposeSource(stream);
  return events;
}

void main() {
  setUpEngineTests();

  test(
    'onBuffering fires, also for a stream created after a dispose',
    () async {
      final pcm = _pcm();
      final first = await _bufferAndUnbuffer(pcm);
      expect(first, contains(false), reason: 'the stream un-buffered');
      await Future<void>.delayed(const Duration(milliseconds: 100));

      final second = await _bufferAndUnbuffer(pcm);
      expect(second, contains(false), reason: 'callbacks still work');
    },
  );

  test('disposeAllSources releases streams with callbacks', () async {
    final stream = SoLoud.instance.setBufferStream(
      bufferingTimeNeeds: 0.3,
      onBuffering: (_, _, _) {},
    );
    SoLoud.instance.play(stream);
    SoLoud.instance.addAudioDataStream(stream, _pcm(seconds: 0.5));
    await SoLoud.instance.disposeAllSources();
    expect(SoLoud.instance.isValidAudioSource(stream), isFalse);
    expect(SoLoud.instance.activeSounds, isEmpty);
  });

  test('short autoDispose buffer streams are disposed when they end', () async {
    // Reported bug: with autoDispose, 1-3 s buffer streams sometimes never
    // triggered voiceEndedCallback, so they were never disposed.
    for (final seconds in [1.0, 1.5, 2.0]) {
      final disposed = Completer<void>();
      final stream = SoLoud.instance.setBufferStream(
        bufferingTimeNeeds: 0.3,
        autoDispose: true,
        bufferingType: BufferingType.released,
      );
      stream.soundEvents.listen((event) {
        if (event.event == SoundEventType.soundDisposed &&
            !disposed.isCompleted) {
          disposed.complete();
        }
      });
      SoLoud.instance.addAudioDataStream(stream, _pcm(seconds: seconds));
      SoLoud.instance.setDataIsEnded(stream);
      SoLoud.instance.play(stream);

      await disposed.future.timeout(
        Duration(milliseconds: (seconds * 1000).round() + 3000),
        onTimeout: () => fail('$seconds s stream: soundDisposed never came'),
      );
      // soundDisposed is emitted before the source leaves activeSounds.
      expect(
        await waitUntil(() => !SoLoud.instance.isValidAudioSource(stream)),
        isTrue,
        reason: '$seconds s stream still valid after soundDisposed',
      );
    }
  }, tags: ['slow']);
}

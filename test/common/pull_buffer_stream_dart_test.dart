// Pull buffer streams driven from Dart, with the real engine and WAV/PCM data
// generated in memory (ports of example/tests/tests/pull_buffer_*.dart,
// which use asset files): the first request, the duration, playback to the
// end, the buffered time range and the trigger position, seeking inside and
// outside the buffer, tiny chunks, resetPullBufferStream and raw PCM.
@Tags(['device'])
library;

import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

/// A pull buffer stream over [bytes] that answers each request with
/// [chunkSize] bytes from the requested offset (the whole rest when null).
class _PullFeeder {
  _PullFeeder(
    this.bytes, {
    this.chunkSize,
    int bufferSizeBytes = 1024 * 1024,
    double bufferTriggerPosition = 0.75,
    BufferType format = BufferType.auto,
    int sampleRate = 44100,
    Channels channels = Channels.stereo,
  }) {
    source = SoLoud.instance.setPullBufferStream(
      bufferSizeBytes: bufferSizeBytes,
      bufferTriggerPosition: bufferTriggerPosition,
      audioSizeBytes: bytes.length,
      format: format,
      sampleRate: sampleRate,
      channels: channels,
      onAudioDuration: (d) {
        durations.add(d);
        if (!duration.isCompleted) duration.complete(d);
      },
      onMoreDataIsNeeded: _onMoreData,
    );
  }

  final Uint8List bytes;
  final int? chunkSize;
  late final AudioSource source;

  /// Every requested offset, in order.
  final requests = <int>[];

  /// Every reported duration, in seconds.
  final durations = <double>[];
  final duration = Completer<double>();

  /// Called on each request before it is answered.
  void Function(int offset)? beforeFeeding;

  /// Whether to answer requests.
  bool feeding = true;

  bool get reachedEnd => _fedEnd;
  var _fedEnd = false;

  void _onMoreData(int offset) {
    requests.add(offset);
    beforeFeeding?.call(offset);
    if (!feeding || offset < 0 || offset >= bytes.length) return;
    final end = chunkSize == null
        ? bytes.length
        : (offset + chunkSize!).clamp(0, bytes.length);
    SoLoud.instance.addPullBufferDataStream(
      source,
      bytes.sublist(offset, end),
      offset: offset,
    );
    if (end >= bytes.length) _fedEnd = true;
  }

  Future<double> waitDuration() =>
      duration.future.timeout(const Duration(seconds: 5));
}

/// The bytes of a [seconds]-long 44.1 kHz stereo 16-bit WAV.
Uint8List _wav(double seconds, {double frequency = 440}) => wavBytes(
  duration: Duration(milliseconds: (seconds * 1000).round()),
  channels: 2,
  frequency: frequency,
);

double _seconds(Duration d) => d.inMicroseconds / 1e6;

void main() {
  setUpEngineTests();
  final soloud = SoLoud.instance;

  /// Waits until [handle] ends, and returns how long that took.
  Future<Duration> playToEnd(
    AudioSource source,
    SoundHandle handle, {
    Duration timeout = const Duration(seconds: 20),
  }) async {
    final watch = Stopwatch()..start();
    final ended = source.soundEvents.firstWhere(
      (e) =>
          e.event == SoundEventType.handleIsNoMoreValid && e.handle == handle,
    );
    await ended.timeout(timeout);
    return watch.elapsed;
  }

  test('the first request is for offset 0', () async {
    final feeder = _PullFeeder(_wav(1))..feeding = false;
    expect(await waitUntil(() => feeder.requests.isNotEmpty), isTrue);
    expect(feeder.requests.first, 0);
  });

  test('reports the duration once, close to the real one', () async {
    final feeder = _PullFeeder(_wav(3), chunkSize: 16 * 1024);
    final d = await feeder.waitDuration();
    expect(d, closeTo(3, 0.05));
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(feeder.durations, hasLength(1));
  });

  test('plays to the end in about the real time', () async {
    final feeder = _PullFeeder(_wav(2), chunkSize: 32 * 1024);
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    final elapsed = await playToEnd(feeder.source, handle);
    expect(feeder.reachedEnd, isTrue);
    expect(_seconds(elapsed), greaterThan(1.5));
    expect(_seconds(elapsed), lessThan(4));
  });

  test('requested offsets advance through the file', () async {
    final bytes = _wav(2);
    final feeder = _PullFeeder(
      bytes,
      chunkSize: 8 * 1024,
      bufferSizeBytes: 128 * 1024,
    );
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await playToEnd(feeder.source, handle);
    final inRange = feeder.requests.where((o) => o < bytes.length).toList();
    for (var i = 1; i < inRange.length; i++) {
      expect(
        inRange[i],
        greaterThanOrEqualTo(inRange[i - 1]),
        reason: 'requests ${feeder.requests}',
      );
    }
    expect(feeder.reachedEnd, isTrue);
  });

  test('the buffered range stays inside the stream', () async {
    final feeder = _PullFeeder(
      _wav(3),
      chunkSize: 16 * 1024,
      bufferSizeBytes: 200 * 1024,
    );
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    for (var i = 0; i < 10; i++) {
      await Future<void>.delayed(const Duration(milliseconds: 150));
      final range = soloud.getPullBufferTimeRange(feeder.source);
      expect(range.startTime, greaterThanOrEqualTo(Duration.zero));
      expect(range.endTime, greaterThanOrEqualTo(range.startTime));
      expect(range.endTime, lessThanOrEqualTo(const Duration(seconds: 3)));
      if (soloud.getIsValidVoiceHandle(handle)) {
        final position = soloud.getPosition(handle);
        expect(
          position,
          lessThanOrEqualTo(range.endTime + const Duration(milliseconds: 100)),
        );
      }
    }
  });

  test('more data is requested near the trigger position', () async {
    const trigger = 0.75;
    final samples = <({double position, double expected})>[];
    SoundHandle? handle;
    late final _PullFeeder feeder;
    feeder =
        _PullFeeder(_wav(6), chunkSize: 4 * 1024, bufferSizeBytes: 200 * 1024)
          ..beforeFeeding = (_) {
            final h = handle;
            if (h == null || !soloud.getIsValidVoiceHandle(h)) return;
            final range = soloud.getPullBufferTimeRange(feeder.source);
            final start = _seconds(range.startTime);
            final end = _seconds(range.endTime);
            samples.add((
              position: _seconds(soloud.getPosition(h)),
              expected: start + trigger * (end - start),
            ));
          };
    await feeder.waitDuration();
    handle = soloud.play(feeder.source);
    await playToEnd(feeder.source, handle);

    // The first requests fill the buffer up to its working size.
    final valid = samples.skip(5).toList();
    expect(valid.length, greaterThanOrEqualTo(5));
    var total = 0.0;
    var worst = 0.0;
    for (final s in valid) {
      final error = (s.position - s.expected).abs();
      total += error;
      if (error > worst) worst = error;
    }
    // 200 KB of stereo float is about 0.58 s.
    expect(total / valid.length, lessThan(0.4));
    expect(worst, lessThan(0.8));
  });

  test('a seek inside the buffer needs no new data', () async {
    final feeder = _PullFeeder(_wav(5), bufferSizeBytes: 20 * 1024 * 1024);
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 800));
    expect(soloud.getPosition(handle), greaterThan(Duration.zero));
    final requestsBefore = feeder.requests.length;

    Future<void> seekTo(Duration target) async {
      soloud.seek(handle, target);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      final position = soloud.getPosition(handle);
      expect(
        (position - target).abs(),
        lessThan(const Duration(milliseconds: 500)),
        reason: 'seek to $target landed at $position',
      );
    }

    var range = soloud.getPullBufferTimeRange(feeder.source);
    expect(range.endTime, greaterThan(range.startTime));
    await seekTo(range.startTime + (range.endTime - range.startTime) * 3 ~/ 10);
    range = soloud.getPullBufferTimeRange(feeder.source);
    await seekTo(range.endTime - const Duration(milliseconds: 500));
    range = soloud.getPullBufferTimeRange(feeder.source);
    await seekTo(range.startTime + const Duration(milliseconds: 100));

    expect(feeder.requests.length, requestsBefore);
    final last = soloud.getPosition(handle);
    await Future<void>.delayed(const Duration(milliseconds: 400));
    expect(soloud.getPosition(handle), greaterThan(last));
  });

  test('a seek outside the buffer refetches from the right offset', () async {
    final bytes = _wav(8);
    final feeder = _PullFeeder(
      bytes,
      chunkSize: 32 * 1024,
      bufferSizeBytes: 256 * 1024,
    );
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 500));
    final requestsBefore = feeder.requests.length;

    const target = Duration(seconds: 6);
    soloud.seek(handle, target);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    final position = soloud.getPosition(handle);
    expect(
      (position - target).abs(),
      lessThan(const Duration(milliseconds: 600)),
      reason: 'position $position',
    );
    // The stream asked for the data at the byte offset of the target (a
    // sequential request still in flight may come first).
    const byteRate = 44100 * 2 * 2;
    final expectedOffset = 44 + _seconds(target) * byteRate;
    final after = feeder.requests.skip(requestsBefore).toList();
    expect(
      after.any((o) => (o - expectedOffset).abs() < byteRate * 0.3),
      isTrue,
      reason: 'requests after the seek: $after, expected ~$expectedOffset',
    );

    final range = soloud.getPullBufferTimeRange(feeder.source);
    expect(range.endTime, lessThanOrEqualTo(const Duration(seconds: 8)));
    await Future<void>.delayed(const Duration(milliseconds: 400));
    expect(soloud.getPosition(handle), greaterThan(position));
  });

  test('seeks near the end keep the range inside the stream', () async {
    final feeder = _PullFeeder(_wav(4), bufferSizeBytes: 256 * 1024);
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    for (var i = 0; i < 3; i++) {
      soloud.seek(handle, Duration(milliseconds: 3500 - i * 100));
      await Future<void>.delayed(const Duration(milliseconds: 100));
      final range = soloud.getPullBufferTimeRange(feeder.source);
      expect(
        range.endTime,
        lessThanOrEqualTo(const Duration(milliseconds: 4100)),
      );
      expect(range.startTime, lessThanOrEqualTo(range.endTime));
      expect(range.startTime, greaterThanOrEqualTo(Duration.zero));
    }
    await playToEnd(feeder.source, handle);
  });

  test('1 KB chunks into a 100 KB buffer still play to the end', () async {
    final feeder = _PullFeeder(
      _wav(2),
      chunkSize: 1024,
      bufferSizeBytes: 100 * 1024,
      bufferTriggerPosition: 0.5,
    );
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 700));
    expect(soloud.getPosition(handle), greaterThan(Duration.zero));
    await playToEnd(feeder.source, handle);
    expect(feeder.reachedEnd, isTrue);
  });

  test('resetPullBufferStream starts over from offset 0', () async {
    final feeder = _PullFeeder(_wav(3), chunkSize: 64 * 1024);
    await feeder.waitDuration();
    final handle = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 600));
    await soloud.stop(handle);

    final before = feeder.requests.length;
    soloud.resetPullBufferStream(feeder.source);
    expect(
      await waitUntil(() => feeder.requests.length > before),
      isTrue,
      reason: 'no request after the reset',
    );
    expect(feeder.requests[before], 0);
    await Future<void>.delayed(const Duration(milliseconds: 100));
    final range = soloud.getPullBufferTimeRange(feeder.source);
    expect(range.startTime, Duration.zero);

    final again = soloud.play(feeder.source);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    final position = soloud.getPosition(again);
    expect(position, greaterThan(Duration.zero));
    expect(position, lessThan(const Duration(milliseconds: 700)));
  });

  test('raw PCM: the duration comes from the size and format', () async {
    // 1.5 s of mono 16-bit samples at 22050 Hz, without a header.
    final wav = wavBytes(
      duration: const Duration(milliseconds: 1500),
      sampleRate: 22050,
    );
    final pcm = wav.sublist(44);
    final feeder = _PullFeeder(
      pcm,
      chunkSize: 8 * 1024,
      format: BufferType.s16le,
      sampleRate: 22050,
      channels: Channels.mono,
    );
    expect(await feeder.waitDuration(), closeTo(1.5, 0.01));
    final handle = soloud.play(feeder.source);
    final elapsed = await playToEnd(feeder.source, handle);
    expect(_seconds(elapsed), greaterThan(1));
    expect(feeder.reachedEnd, isTrue);
  });

  test('audioSizeBytes 0 is rejected', () {
    expect(
      soloud.setPullBufferStream,
      throwsA(isA<SoLoudInvalidParameterException>()),
    );
  });

  test('the pull buffer API rejects other sources', () async {
    final sound = await loadTestSound();
    expect(
      () => soloud.getPullBufferTimeRange(sound),
      throwsA(isA<SoLoudException>()),
    );
    expect(
      () => soloud.resetPullBufferStream(sound),
      throwsA(isA<SoLoudException>()),
    );
  });

  test('addPullBufferDataStream rejects other sources', () async {
    final sound = await loadTestSound();
    expect(
      () => soloud.addPullBufferDataStream(sound, Uint8List(16)),
      throwsA(isA<SoLoudException>()),
    );
  });
}

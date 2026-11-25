// Realtime streams (`SoLoud.setRealtimeStream`) with the real engine: the
// ring buffer's read/write counters, a full buffer, reset, Float32 and Int16
// pushes heard on the mixer output, clocked and scheduled pushes, underruns,
// the in-process feeder, a stream disposed while it is fed, and two streams
// playing at once.
@Tags(['device'])
library;

import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

/// The engine sample rate (the default of `SoLoud.init`); the streams use it
/// too, so nothing is resampled.
const _rate = 44100;

/// [frames] frames of a [hz] sine at amplitude 0.5, interleaved on
/// [channels] channels.
Float32List _sine(int frames, {double hz = 440, int channels = 2}) {
  final out = Float32List(frames * channels);
  for (var f = 0; f < frames; f++) {
    final v = 0.5 * math.sin(2 * math.pi * hz * f / _rate);
    for (var c = 0; c < channels; c++) {
      out[f * channels + c] = v;
    }
  }
  return out;
}

Int16List _toInt16(Float32List samples) => Int16List.fromList([
  for (final s in samples) (s * 32767).round().clamp(-32768, 32767),
]);

/// Feeds a mono sine at the phase's frequency from the event loop (see
/// `startFloat32Feeder`).
int _feedSine(
  Float32List out,
  int frames,
  _Phase phase,
  void Function(Object?) emit,
) {
  for (var f = 0; f < frames; f++) {
    out[f] = 0.5 * math.sin(phase.value);
    phase.value += 2 * math.pi * phase.hz / _rate;
  }
  emit(frames);
  return frames;
}

class _Phase {
  _Phase(this.hz);
  final double hz;
  double value = 0;
}

void main() {
  setUpEngineTests(globalVolume: 0.2);
  final soloud = SoLoud.instance;

  RealtimeStreamHandle newStream({
    Channels channels = Channels.stereo,
    BufferType format = BufferType.f32le,
    int bufferFrames = 1 << 16,
  }) => soloud.setRealtimeStream(
    sampleRate: _rate,
    channels: channels,
    format: format,
    bufferFrames: bufferFrames,
  );

  group('ring buffer', () {
    test('starts empty; pushes before play are queued', () {
      final stream = newStream(bufferFrames: 4096);
      expect(stream.availableToRead, 0);
      final capacity = stream.availableToWrite;
      expect(capacity, greaterThanOrEqualTo(4096));
      expect(stream.underrunCount, 0);

      expect(stream.pushFloat32(_sine(1000)), 1000);
      expect(stream.availableToRead, 1000);
      expect(stream.availableToWrite, capacity - 1000);
    });

    test('a full buffer accepts only what fits, then nothing', () {
      final stream = newStream(bufferFrames: 4096);
      final capacity = stream.availableToWrite;
      expect(stream.pushFloat32(_sine(capacity + 500)), capacity);
      expect(stream.availableToWrite, 0);
      expect(stream.availableToRead, capacity);
      expect(stream.pushFloat32(_sine(10)), 0);
      expect(stream.pushInt16(Int16List(20)), 0);
    });

    test('reset empties the buffer', () {
      final stream = newStream(bufferFrames: 4096)..pushFloat32(_sine(2000));
      expect(stream.availableToRead, 2000);
      stream.reset();
      expect(stream.availableToRead, 0);
      expect(stream.pushFloat32(_sine(10)), 10);
    });

    test('Int16 and planar pushes count frames, not samples', () {
      final stereo = newStream(bufferFrames: 4096);
      expect(stereo.pushInt16(Int16List(2 * 300)), 300);
      expect(stereo.availableToRead, 300);

      final planar = newStream(
        format: BufferType.f32lePlanar,
        bufferFrames: 4096,
      );
      expect(planar.pushFloat32(Float32List(2 * 300)), 300);
      expect(planar.availableToRead, 300);

      final mono = newStream(channels: Channels.mono, bufferFrames: 4096);
      expect(mono.pushFloat32(Float32List(300)), 300);
      expect(mono.availableToRead, 300);
    });

    test('invalid parameters throw', () {
      expect(
        () => soloud.setRealtimeStream(format: BufferType.auto),
        throwsArgumentError,
      );
      expect(
        () => soloud.setRealtimeStream(sampleRate: 0),
        throwsA(isA<SoLoudCppException>()),
      );
      expect(
        () => soloud.setRealtimeStream(bufferFrames: 0),
        throwsA(isA<SoLoudCppException>()),
      );
    });
  });

  group('output', () {
    test('a Float32 push is heard', () async {
      final stream = newStream()..pushFloat32(_sine(_rate * 2));
      soloud.play(stream.source);
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.02));
      expect(out.frequency(0), closeTo(440, 5));
      expect(out.frequency(1), closeTo(440, 5));
    });

    test('an Int16 push is heard', () async {
      final stream = newStream(format: BufferType.s16le)
        ..pushInt16(_toInt16(_sine(_rate * 2, hz: 660)));
      soloud.play(stream.source);
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.02));
      expect(out.frequency(0), closeTo(660, 5));
    });

    test('a planar push keeps its channels apart', () async {
      const frames = _rate * 2;
      final left = _sine(frames, channels: 1);
      final planar = Float32List(frames * 2)..setAll(0, left);
      final stream = newStream(format: BufferType.f32lePlanar)
        ..pushFloat32(planar);
      soloud.play(stream.source);
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.02));
      expect(out.rms(1), lessThan(0.001), reason: 'right plane is silent');
    });

    test('the voice drains the buffer', () async {
      final stream = newStream()..pushFloat32(_sine(_rate ~/ 4));
      soloud.play(stream.source);
      expect(await waitUntil(() => stream.availableToRead == 0), isTrue);
    });

    test('two streams play at once', () async {
      final a = newStream(channels: Channels.mono)
        ..pushFloat32(_sine(_rate * 2, channels: 1));
      final b = newStream(channels: Channels.mono)
        ..pushFloat32(_sine(_rate * 2, hz: 1000, channels: 1));
      soloud
        ..play(a.source, pan: -1)
        ..play(b.source, pan: 1);
      final out = await captureOutput();
      expect(out.frequency(0), closeTo(440, 5));
      expect(out.frequency(1), closeTo(1000, 10));
    });

    test('the in-process feeder keeps it playing without underruns', () async {
      final stream = newStream(channels: Channels.mono, bufferFrames: 8192);
      var fed = 0;
      final feeder = await stream.startFloat32Feeder<_Phase>(
        initialState: _Phase(440),
        onFeed: _feedSine,
        onUiEvent: (frames) => fed += frames! as int,
        // The device's first pull can take two engine quanta (4096 frames
        // on macOS) at once: keep more than that buffered.
        targetBufferedFrames: 6000,
        useIsolate: false,
      );
      addTearDown(feeder.dispose);
      expect(feeder.isUsingIsolate, isFalse);
      expect(await waitUntil(() => stream.availableToRead >= 6000), isTrue);
      soloud.play(stream.source);
      final out = await captureOutput(frames: 16384);
      expect(
        out.frequency(0),
        closeTo(440, 5),
        reason: 'underruns: ${stream.underrunCount}',
      );
      expect(fed, greaterThan(16384));
      expect(stream.underrunCount, 0);
      await feeder.dispose();
      expect(feeder.isDisposed, isTrue);
    });
  });

  group('underruns', () {
    test('playing before the first push is not an underrun; running dry '
        'once is one, and staying dry adds none', () async {
      final stream = newStream();
      soloud.play(stream.source);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(stream.underrunCount, 0);

      stream.pushFloat32(_sine(_rate ~/ 10));
      expect(await waitUntil(() => stream.underrunCount == 1), isTrue);
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(stream.underrunCount, 1);

      stream.pushFloat32(_sine(_rate ~/ 10));
      expect(await waitUntil(() => stream.underrunCount == 2), isTrue);

      stream.reset();
      expect(stream.underrunCount, 0);
    });
  });

  group('timed pushes', () {
    test('a scheduled push is silent until its engine time', () async {
      final stream = newStream();
      soloud.play(stream.source);
      // Let the voice start, so the push is placed against it.
      await Future<void>.delayed(const Duration(milliseconds: 100));
      final at = soloud.getEngineTime() + const Duration(milliseconds: 700);
      expect(stream.pushScheduledFloat32(at, _sine(_rate)), _rate);

      final before = await captureOutput(frames: 2048, skipFrames: 0);
      expect(before.peak(0), lessThan(0.001), reason: 'not started yet');

      expect(
        await waitUntil(
          () => soloud.getEngineTime() > at + const Duration(milliseconds: 50),
        ),
        isTrue,
      );
      final after = await captureOutput(frames: 4096, skipFrames: 0);
      expect(after.rms(0), greaterThan(0.02));
    });

    test('a scheduled push in the past plays at once', () async {
      final stream = newStream();
      soloud.play(stream.source);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      expect(stream.pushScheduledFloat32(Duration.zero, _sine(_rate)), _rate);
      final out = await captureOutput(frames: 4096);
      expect(out.rms(0), greaterThan(0.02));
    });

    test('clocked pushes are queued and played', () async {
      final stream = newStream();
      soloud.play(stream.source);
      await Future<void>.delayed(const Duration(milliseconds: 100));
      const chunk = 4410;
      var total = 0;
      for (var i = 0; i < 10; i++) {
        total += stream.pushClockedFloat32(
          Duration(milliseconds: 100 * i),
          _sine(chunk),
        );
      }
      expect(total, 10 * chunk);
      final out = await captureOutput(frames: 4096);
      expect(out.rms(0), greaterThan(0.02));
      expect(out.frequency(0), closeTo(440, 5));
    });

    test('timed pushes before play are queued like plain pushes', () {
      final stream = newStream(bufferFrames: 4096);
      expect(stream.pushClockedFloat32(Duration.zero, _sine(100)), 100);
      expect(
        stream.pushScheduledFloat32(const Duration(seconds: 1000), _sine(100)),
        100,
      );
      expect(stream.availableToRead, 200);
    });
  });

  group('disposal', () {
    test('pushing to a disposed stream writes nothing', () async {
      final stream = newStream(bufferFrames: 4096);
      final h = soloud.play(stream.source);
      stream.pushFloat32(_sine(1000));
      await soloud.disposeSource(stream.source);
      expect(soloud.getIsValidVoiceHandle(h), isFalse);
      expect(stream.pushFloat32(_sine(1000)), 0);
      expect(stream.pushInt16(Int16List(200)), 0);
      expect(stream.availableToRead, 0);
      expect(stream.availableToWrite, 0);
      expect(stream.underrunCount, 0);
      expect(stream.reset, throwsA(isA<SoLoudCppException>()));
    });

    test('disposing while the feeder runs stops it cleanly', () async {
      final stream = newStream(channels: Channels.mono, bufferFrames: 8192);
      final feeder = await stream.startFloat32Feeder<_Phase>(
        initialState: _Phase(440),
        onFeed: _feedSine,
        useIsolate: false,
      );
      addTearDown(feeder.dispose);
      soloud.play(stream.source);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      await soloud.disposeSource(stream.source);
      // The feeder keeps ticking against a stream that is gone.
      await Future<void>.delayed(const Duration(milliseconds: 200));
      expect(stream.availableToRead, 0);
      await feeder.dispose();
    });

    test(
      'pushing to a hash that is not a realtime stream writes nothing',
      () async {
        final sound = await loadSine();
        final fake = RealtimeStreamHandle.fromHash(
          soundHash: sound.soundHash,
          sampleRate: _rate,
          channels: Channels.mono,
        );
        expect(fake.pushFloat32(Float32List(100)), 0);
        expect(fake.availableToRead, 0);
        expect(fake.reset, throwsA(isA<SoLoudCppException>()));
      },
    );
  });
}

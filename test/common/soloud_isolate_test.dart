// SoLoudIsolate from worker isolates, with the real engine: the engine state
// seen by a worker, a realtime stream attached and fed from a worker (Float32
// and Int16, heard on the mixer output), its counters read from a worker, a
// worker killed while it pushes, the isolate-backed feeder, the mixer output
// WAV header, reading samples from memory, and feeding a buffer stream
// (addAudioDataStream) from a worker. Mixer output capture from workers is
// in mixer_output_isolate_test.dart.
@TestOn('vm')
@Tags(['device'])
library;

import 'dart:async';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

const _rate = 44100;

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

/// Attaches to the stereo f32 stream [hash] in the calling isolate.
RealtimeStreamHandle _attach(
  int hash, {
  BufferType format = BufferType.f32le,
}) => SoLoudIsolate.instance.attachRealtimeStream(
  soundHash: SoundHash.fromValue(hash),
  sampleRate: _rate,
  format: format,
);

/// Runs [work] with [arg] in a new worker isolate. The closure sent holds
/// only these two, unlike a closure written inside `main` (which would also
/// hold the engine's objects, which cannot be sent).
Future<R> _inWorker<A, R>(FutureOr<R> Function(A arg) work, A arg) =>
    Isolate.run(() => work(arg));

bool _isInitialized(void _) => SoLoudIsolate.instance.isInitialized;

int _pushF32(List<int> hashAndFrames) =>
    _attach(hashAndFrames[0]).pushFloat32(_sine(hashAndFrames[1]));

int _pushS16(List<int> hashAndFrames) =>
    SoLoudIsolate.instance.pushRealtimeInt16(
      SoundHash.fromValue(hashAndFrames[0]),
      Int16List(2 * hashAndFrames[1]),
      hashAndFrames[1],
    );

List<int> _counters(int hash) {
  final h = SoundHash.fromValue(hash);
  final iso = SoLoudIsolate.instance;
  return [
    iso.getRealtimeAvailableToRead(h),
    iso.getRealtimeAvailableToWrite(h),
    iso.getRealtimeUnderrunCount(h),
  ];
}

Uint8List _wavHeader(void _) =>
    SoLoudIsolate.instance.getMixerOutputWavHeader();

Future<Float32List> _readSamples(Uint8List bytes) =>
    SoLoudIsolate.instance.readSamplesFromMem(bytes, 100);

List<Object> _afterDeinit(int hash) {
  final iso = SoLoudIsolate.instance;
  String header;
  try {
    iso.getMixerOutputWavHeader();
    header = 'no exception';
  } on SoLoudNotInitializedException {
    header = 'not initialized';
  }
  return [
    iso.isInitialized,
    _attach(hash).pushFloat32(_sine(100)),
    iso.getRealtimeAvailableToRead(SoundHash.fromValue(hash)),
    header,
  ];
}

/// Keeps pushing a tone to [args] = (hash, ready port) until killed.
Future<void> _pushForever(List<Object> args) async {
  final hash = args[0] as int;
  final ready = args[1] as SendPort;
  final chunk = _sine(512);
  var sent = false;
  while (true) {
    SoLoudIsolate.instance.pushRealtimeFloat32(
      SoundHash.fromValue(hash),
      chunk,
      512,
    );
    if (!sent) {
      ready.send('pushing');
      sent = true;
    }
    // Yield, so the kill message is handled.
    await Future<void>.delayed(Duration.zero);
  }
}

class _Tone {
  _Tone(this.hz);
  final double hz;
  double phase = 0;
}

int _feedTone(
  Float32List out,
  int frames,
  _Tone tone,
  void Function(Object?) emit,
) {
  for (var f = 0; f < frames; f++) {
    out[f] = 0.5 * math.sin(tone.phase);
    tone.phase += 2 * math.pi * tone.hz / _rate;
  }
  emit(frames);
  return frames;
}

/// Feeds half a second of silence to the s8 buffer stream [stream] and ends
/// it. Returns the type of the error thrown, if any.
String? _feedBufferStream(AudioSource stream) {
  try {
    SoLoud.instance
      ..addAudioDataStream(stream, Uint8List(_rate ~/ 2))
      ..setDataIsEnded(stream);
    return null;
  } on Object catch (e) {
    return '${e.runtimeType}';
  }
}

void main() {
  setUpEngineTests(globalVolume: 0.2);
  final soloud = SoLoud.instance;

  RealtimeStreamHandle newStream({
    Channels channels = Channels.stereo,
    BufferType format = BufferType.f32le,
  }) => soloud.setRealtimeStream(
    sampleRate: _rate,
    channels: channels,
    format: format,
    bufferFrames: 1 << 16,
  );

  test('a worker sees the engine initialized', () async {
    expect(await _inWorker(_isInitialized, null), isTrue);
  });

  group('realtime stream', () {
    test(
      'a worker pushes Float32 and Int16 frames the main isolate sees',
      () async {
        final f32 = newStream();
        final hash = f32.soundHash.hash;
        final written = await _inWorker(_pushF32, [hash, 1000]);
        expect(written, 1000);
        expect(f32.availableToRead, 1000);

        final s16 = newStream(format: BufferType.s16le);
        final hash16 = s16.soundHash.hash;
        final written16 = await _inWorker(_pushS16, [hash16, 700]);
        expect(written16, 700);
        expect(s16.availableToRead, 700);
      },
    );

    test('a worker reads the same counters as the main isolate', () async {
      final stream = newStream()..pushFloat32(_sine(3000));
      final hash = stream.soundHash.hash;
      final counters = await _inWorker(_counters, hash);
      expect(counters, [
        stream.availableToRead,
        stream.availableToWrite,
        stream.underrunCount,
      ]);
      expect(counters[0], 3000);
    });

    test('a worker reads the underrun count', () async {
      final stream = newStream()..pushFloat32(_sine(_rate ~/ 10));
      soloud.play(stream.source);
      expect(await waitUntil(() => stream.underrunCount == 1), isTrue);
      final hash = stream.soundHash.hash;
      expect((await _inWorker(_counters, hash))[2], 1);
    });

    test('frames pushed by a worker are heard', () async {
      final stream = newStream();
      final hash = stream.soundHash.hash;
      await _inWorker(_pushF32, [hash, _rate * 2]);
      soloud.play(stream.source);
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.02));
      expect(out.frequency(0), closeTo(440, 5));
    });

    test('a worker killed while it pushes leaves the stream usable', () async {
      final stream = newStream();
      soloud.play(stream.source);
      final ready = ReceivePort();
      final exited = ReceivePort();
      final worker = await Isolate.spawn(_pushForever, <Object>[
        stream.soundHash.hash,
        ready.sendPort,
      ], onExit: exited.sendPort);
      expect(await ready.first, 'pushing');
      await Future<void>.delayed(const Duration(milliseconds: 100));
      worker.kill(priority: Isolate.immediate);
      await exited.first;
      ready.close();
      exited.close();

      stream.reset();
      expect(stream.availableToRead, 0);
      expect(stream.pushFloat32(_sine(_rate)), _rate);
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.02));
      await soloud.disposeSource(stream.source);
    });

    test('pushing from a worker after the stream is disposed writes '
        'nothing', () async {
      final stream = newStream();
      final hash = stream.soundHash.hash;
      await soloud.disposeSource(stream.source);
      expect(await _inWorker(_pushF32, [hash, 100]), 0);
    });

    test('the isolate-backed feeder plays from a worker', () async {
      final stream = soloud.setRealtimeStream(
        sampleRate: _rate,
        channels: Channels.mono,
        bufferFrames: 8192,
      );
      var fed = 0;
      final feeder = await stream.startFloat32Feeder<_Tone>(
        initialState: _Tone(440),
        onFeed: _feedTone,
        onUiEvent: (frames) => fed += frames! as int,
        // The device's first pull can take two engine quanta (4096 frames
        // on macOS) at once: keep more than that buffered.
        targetBufferedFrames: 6000,
      );
      addTearDown(feeder.dispose);
      expect(feeder.isUsingIsolate, isTrue);
      expect(await waitUntil(() => stream.availableToRead >= 6000), isTrue);
      soloud.play(stream.source);
      final out = await captureOutput(frames: 16384);
      expect(out.frequency(0), closeTo(440, 5));
      expect(stream.underrunCount, 0);
      expect(fed, greaterThan(16384));
      await feeder.dispose();
      expect(feeder.isDisposed, isTrue);
      // The worker no longer refills.
      final level = stream.availableToRead;
      await Future<void>.delayed(const Duration(milliseconds: 300));
      expect(stream.availableToRead, lessThanOrEqualTo(level));
    });
  });

  group('mixer output WAV header', () {
    test('a worker reads the final header of a stopped WAV capture', () async {
      soloud.play(await loadTestWaveform(), volume: 0.2);
      var bytes = 0;
      final subscription = soloud
          .startMixerOutputStream(format: MixerOutputFormat.wav)
          .listen((chunk) => bytes += chunk.length);
      await waitUntil(() => bytes > 8192);
      // The header is written when the capture stops (it holds the sizes).
      soloud.stopMixerOutputStream();
      await subscription.cancel();
      final header = await _inWorker(_wavHeader, null);
      expect(header, hasLength(44));
      expect(String.fromCharCodes(header.sublist(0, 4)), 'RIFF');
      expect(String.fromCharCodes(header.sublist(8, 12)), 'WAVE');
      final b = ByteData.sublistView(header);
      // The engine's format (the defaults of SoLoud.init).
      expect(b.getUint16(22, Endian.little), 2, reason: 'channels');
      expect(b.getUint32(24, Endian.little), 44100, reason: 'sample rate');
    });
  });

  test('a worker reads samples from memory', () async {
    final bytes = wavBytes(duration: const Duration(milliseconds: 500));
    final samples = await _inWorker(_readSamples, bytes);
    expect(samples, hasLength(100));
    expect(samples.any((s) => s.abs() > 0.1), isTrue);
  });

  test('a worker feeds a buffer stream', () async {
    // The documented pattern (example/lib/buffer_stream/generate.dart): the
    // stream is created in the main isolate and fed inside compute().
    final stream = soloud.setBufferStream(
      maxBufferSizeBytes: 1024 * 1024,
      format: BufferType.s8,
      sampleRate: _rate,
    );
    final error = await _inWorker(_feedBufferStream, stream);
    expect(error, isNull);
    expect(soloud.getBufferSize(stream), greaterThan(0));
    final handle = soloud.play(stream);
    expect(soloud.getIsValidVoiceHandle(handle), isTrue);
  });

  test('a worker cannot feed a buffer stream after deinit', () async {
    final stream = soloud.setBufferStream(
      maxBufferSizeBytes: 1024 * 1024,
      format: BufferType.s8,
    );
    soloud.deinit();
    final error = await _inWorker(_feedBufferStream, stream);
    expect(error, 'SoLoudNotInitializedException');
  });

  test('after deinit a worker sees no engine and pushes nothing', () async {
    final stream = newStream();
    final hash = stream.soundHash.hash;
    soloud.deinit();
    final result = await _inWorker(_afterDeinit, hash);
    expect(result, [false, 0, 0, 'not initialized']);
  });
}

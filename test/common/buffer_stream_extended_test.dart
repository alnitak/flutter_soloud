// Buffer streams with the real engine: buffer size, reset, time consumed, in
// the preserved and released buffering modes.
@Tags(['device'])
library;

import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// One second of f32le stereo noise at 44.1 kHz.
Uint8List _noise() {
  final random = Random(1);
  final data = Float32List(44100 * 2);
  for (var i = 0; i < data.length; i++) {
    data[i] = (random.nextDouble() * 2 - 1) * 0.5;
  }
  return data.buffer.asUint8List();
}

void main() {
  setUpEngineTests();

  test('preserved: the buffer grows with data and reset empties it', () async {
    final pcm = _noise();
    final stream = SoLoud.instance.setBufferStream(
      format: BufferType.f32le,
      bufferingTimeNeeds: 1,
      maxBufferSizeBytes: 5 * 1024 * 1024,
    );
    SoLoud.instance.addAudioDataStream(stream, pcm);
    final size1 = SoLoud.instance.getBufferSize(stream);
    expect(size1, greaterThan(0));

    SoLoud.instance.play(stream);
    await Future<void>.delayed(const Duration(milliseconds: 200));
    SoLoud.instance.addAudioDataStream(stream, pcm);
    final size2 = SoLoud.instance.getBufferSize(stream);
    expect(size2, greaterThan(size1), reason: 'preserved data is kept');

    SoLoud.instance.resetBufferStream(stream);
    expect(SoLoud.instance.getBufferSize(stream), lessThan(size2));

    SoLoud.instance.addAudioDataStream(stream, pcm);
    SoLoud.instance.setDataIsEnded(stream);
    expect(SoLoud.instance.getBufferSize(stream), greaterThan(0));
    await SoLoud.instance.disposeSource(stream);
  });

  test('released: time consumed advances while playing', () async {
    final pcm = _noise();
    final stream = SoLoud.instance.setBufferStream(
      format: BufferType.f32le,
      bufferingTimeNeeds: 0.5,
      bufferingType: BufferingType.released,
      maxBufferSizeBytes: 2 * 1024 * 1024,
    );
    SoLoud.instance.addAudioDataStream(stream, pcm);
    final handle = SoLoud.instance.play(stream);

    expect(
      await waitUntil(
        () => SoLoud.instance.getStreamTimeConsumed(stream) > Duration.zero,
      ),
      isTrue,
      reason: 'the consumed time grows as the stream plays',
    );
    final consumed = SoLoud.instance.getStreamTimeConsumed(stream);
    SoLoud.instance.addAudioDataStream(stream, pcm);
    expect(
      await waitUntil(
        () => SoLoud.instance.getStreamTimeConsumed(stream) > consumed,
      ),
      isTrue,
    );
    SoLoud.instance.setDataIsEnded(stream);
    expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
    await SoLoud.instance.disposeSource(stream);
  });
}

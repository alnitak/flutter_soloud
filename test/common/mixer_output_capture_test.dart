// Mixer output capture in every output format, with the real engine: each
// format delivers data with its container's magic number, fixed-size PCM
// chunking honours the requested sample rate and channels, and compressed
// formats work with a notification threshold. Stopping and isolates are in
// mixer_output_stop_test.dart, mixer_output_isolate_test.dart and
// mixer_output_reinit_test.dart.
@Tags(['device'])
library;

import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

// ignore_for_file: experimental_member_use

/// Captures [format] until [enough] holds for the chunks received (or a
/// timeout), stops the capture and returns the chunks, tail included.
Future<List<Uint8List>> _capture(
  MixerOutputFormat format, {
  bool Function(List<Uint8List> chunks)? enough,
  int sampleRate = -1,
  int channels = -1,
  int chunkPCMFrames = -1,
  int notificationThresholdBytes = 4096,
}) async {
  final chunks = <Uint8List>[];
  final errors = <Object>[];
  final subscription = SoLoud.instance
      .startMixerOutputStream(
        format: format,
        sampleRate: sampleRate,
        channels: channels,
        chunkPCMFrames: chunkPCMFrames,
        notificationThresholdBytes: notificationThresholdBytes,
      )
      .listen(chunks.add, onError: errors.add);
  expect(SoLoud.instance.isMixerOutputStreamRunning, isTrue);
  await waitUntil(() => (enough ?? (c) => c.isNotEmpty)(chunks));
  // Stop before cancelling, so the tail is delivered through the stream.
  SoLoud.instance.stopMixerOutputStream();
  await subscription.cancel();
  expect(errors, isEmpty, reason: '$format stream errors');
  return chunks;
}

String _magic(Uint8List bytes) => String.fromCharCodes(bytes.take(4));

void main() {
  setUpEngineTests();
  final xiph = SoLoudController().soLoudFFI.areXiphLibsAvailable();

  setUp(() async {
    final sound = await loadTestWaveform(waveform: WaveForm.square);
    SoLoud.instance.play(sound, looping: true, volume: 0.3);
  });

  const magic = {
    MixerOutputFormat.opus: 'OggS',
    MixerOutputFormat.vorbis: 'OggS',
    MixerOutputFormat.flac: 'fLaC',
    MixerOutputFormat.wav: 'RIFF',
  };
  const compressed = {MixerOutputFormat.opus, MixerOutputFormat.vorbis};

  for (final format in MixerOutputFormat.values) {
    test(
      '$format delivers data${magic[format] == null ? '' : ' starting with '
                '${magic[format]}'}',
      () async {
        final chunks = await _capture(format);
        final total = chunks.fold<int>(0, (sum, c) => sum + c.length);
        expect(total, greaterThan(0), reason: 'no data captured');
        final expected = magic[format];
        if (expected != null) {
          expect(chunks.first.length, greaterThan(4));
          expect(_magic(chunks.first), expected);
        }
      },
      skip: compressed.contains(format) && !xiph
          ? 'Xiph libraries not available'
          : false,
    );
  }

  for (final format in [
    MixerOutputFormat.pcmF32le,
    MixerOutputFormat.pcmS16le,
  ]) {
    test('$format chunkPCMFrames gives fixed-size chunks', () async {
      const frames = 4096;
      const channels = 2;
      final chunkBytes = frames * channels * format.bytesPerSample;
      final chunks = await _capture(
        format,
        sampleRate: 44100,
        channels: channels,
        chunkPCMFrames: frames,
        enough: (c) => c.length >= 4,
      );
      expect(chunks.length, greaterThanOrEqualTo(4));
      expect(
        chunks.take(chunks.length - 1).map((c) => c.length),
        everyElement(chunkBytes),
        reason: 'only the tail may be shorter than a chunk',
      );
      expect(chunks.last.length, lessThanOrEqualTo(chunkBytes));
    });
  }

  for (final format in compressed) {
    test('$format with notificationThresholdBytes captures data', () async {
      final chunks = await _capture(
        format,
        notificationThresholdBytes: 8192,
        enough: (c) => c.length >= 2,
      );
      expect(chunks, isNotEmpty);
      expect(_magic(chunks.first), 'OggS');
    }, skip: xiph ? false : 'Xiph libraries not available');
  }
}

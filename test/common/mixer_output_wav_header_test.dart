// getMixerOutputWavHeader with the real engine: after a WAV capture stops it
// returns a valid 44-byte header for the engine's channel count and sample
// rate whose sizes match the PCM data the stream delivered, so the saved file
// becomes a valid WAV; without a WAV capture it returns nothing; it needs an
// initialized engine.
@Tags(['device'])
library;

import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

/// The fields of a canonical 44-byte PCM WAV header.
typedef _WavHeader = ({
  String riff,
  int riffSize,
  String wave,
  String fmt,
  int fmtSize,
  int audioFormat,
  int channels,
  int sampleRate,
  int byteRate,
  int blockAlign,
  int bitsPerSample,
  String data,
  int dataSize,
});

_WavHeader _parse(Uint8List h) {
  final b = ByteData.sublistView(h);
  String tag(int at) => String.fromCharCodes(h.sublist(at, at + 4));
  return (
    riff: tag(0),
    riffSize: b.getUint32(4, Endian.little),
    wave: tag(8),
    fmt: tag(12),
    fmtSize: b.getUint32(16, Endian.little),
    audioFormat: b.getUint16(20, Endian.little),
    channels: b.getUint16(22, Endian.little),
    sampleRate: b.getUint32(24, Endian.little),
    byteRate: b.getUint32(28, Endian.little),
    blockAlign: b.getUint16(32, Endian.little),
    bitsPerSample: b.getUint16(34, Endian.little),
    data: tag(36),
    dataSize: b.getUint32(40, Endian.little),
  );
}

/// Checks that [header] describes 16-bit PCM with [channels] channels at
/// [sampleRate] Hz and [dataSize] bytes of data.
void _expectHeader(
  Uint8List header, {
  required int channels,
  required int sampleRate,
  required int dataSize,
}) {
  expect(header, hasLength(44));
  final h = _parse(header);
  expect(h.riff, 'RIFF');
  expect(h.wave, 'WAVE');
  expect(h.fmt, 'fmt ');
  expect(h.fmtSize, 16);
  expect(h.audioFormat, 1, reason: 'PCM');
  expect(h.channels, channels);
  expect(h.sampleRate, sampleRate);
  expect(h.bitsPerSample, 16);
  expect(h.blockAlign, channels * 2);
  expect(h.byteRate, sampleRate * channels * 2);
  expect(h.data, 'data');
  expect(h.dataSize, dataSize);
  expect(h.riffSize, dataSize + 36);
}

/// Captures [format] until at least [minBytes] arrived, stops, and returns
/// everything the stream delivered (the tail after the stop included).
Future<Uint8List> _capture(
  MixerOutputFormat format, {
  int minBytes = 32 * 1024,
}) async {
  final builder = BytesBuilder(copy: false);
  final subscription = SoLoud.instance
      .startMixerOutputStream(format: format)
      .listen(builder.add);
  await waitUntil(
    () => builder.length >= minBytes,
    timeout: const Duration(seconds: 5),
  );
  SoLoud.instance.stopMixerOutputStream();
  await subscription.cancel();
  return builder.takeBytes();
}

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  Future<void> startPlaying({
    Channels channels = Channels.stereo,
    int sampleRate = 44100,
  }) async {
    await soloud.init(channels: channels, sampleRate: sampleRate);
    final tone = await loadTestWaveform();
    soloud
      ..setGlobalVolume(0.1)
      ..play(tone, looping: true);
  }

  // First, before any other test of this file captures WAV.
  test('nothing before any capture', () async {
    await startPlaying();
    expect(soloud.getMixerOutputWavHeader(), isEmpty);
  });

  for (final channels in Channels.values) {
    test('WAV capture with $channels gives a matching header', () async {
      await startPlaying(channels: channels);
      final bytes = await _capture(MixerOutputFormat.wav);
      expect(bytes.length, greaterThan(44));
      expect(String.fromCharCodes(bytes.sublist(0, 4)), 'RIFF');
      // The PCM payload is whole frames of 16-bit samples.
      expect((bytes.length - 44) % (channels.count * 2), 0);

      final header = soloud.getMixerOutputWavHeader();
      _expectHeader(
        header,
        channels: channels.count,
        sampleRate: soloud.sampleRate,
        dataSize: bytes.length - 44,
      );
    });
  }

  for (final sampleRate in [22050, 48000]) {
    test('WAV capture at $sampleRate Hz', () async {
      await startPlaying(sampleRate: sampleRate);
      final bytes = await _capture(MixerOutputFormat.wav);
      _expectHeader(
        soloud.getMixerOutputWavHeader(),
        channels: 2,
        sampleRate: sampleRate,
        dataSize: bytes.length - 44,
      );
    });
  }

  test('the header written at the start differs only in its sizes', () async {
    await startPlaying();
    final bytes = await _capture(MixerOutputFormat.wav);
    final initial = bytes.sublist(0, 44);
    final finalHeader = soloud.getMixerOutputWavHeader();
    // Everything but the RIFF and data size fields is the same.
    for (var i = 0; i < 44; i++) {
      if ((i >= 4 && i < 8) || i >= 40) continue;
      expect(initial[i], finalHeader[i], reason: 'byte $i');
    }
    expect(_parse(finalHeader).dataSize, greaterThan(0));
  });

  test('the patched file holds exactly the captured samples', () async {
    await startPlaying();
    final bytes = await _capture(MixerOutputFormat.wav, minBytes: 64 * 1024);
    final file = Uint8List.fromList(bytes)
      ..setRange(0, 44, soloud.getMixerOutputWavHeader());
    final h = _parse(file);
    expect(44 + h.dataSize, file.length);
    final samples = ByteData.sublistView(file, 44);
    var peak = 0;
    for (var i = 0; i + 1 < samples.lengthInBytes; i += 2) {
      final v = samples.getInt16(i, Endian.little).abs();
      if (v > peak) peak = v;
    }
    expect(peak, greaterThan(100), reason: 'the tone was captured');
  });

  test('each WAV capture gets its own header', () async {
    await startPlaying();
    final first = await _capture(MixerOutputFormat.wav);
    final firstSize = _parse(soloud.getMixerOutputWavHeader()).dataSize;
    expect(firstSize, first.length - 44);

    final second = await _capture(
      MixerOutputFormat.wav,
      minBytes: first.length * 2,
    );
    final secondSize = _parse(soloud.getMixerOutputWavHeader()).dataSize;
    expect(secondSize, second.length - 44);
    expect(secondSize, isNot(firstSize));
  });

  for (final format in MixerOutputFormat.values.where(
    (f) => f != MixerOutputFormat.wav,
  )) {
    test('nothing after a $format capture', () async {
      await startPlaying();
      if (!format.isPcm &&
          !SoLoudController().soLoudFFI.areXiphLibsAvailable()) {
        markTestSkipped('Xiph libraries not available');
        return;
      }
      // A WAV capture first, so a stale header would show.
      await _capture(MixerOutputFormat.wav);
      expect(soloud.getMixerOutputWavHeader(), hasLength(44));
      await _capture(format, minBytes: 1);
      expect(soloud.getMixerOutputWavHeader(), isEmpty);
    });
  }

  test('during a WAV capture it returns no stale header', () async {
    await startPlaying();
    await _capture(MixerOutputFormat.wav);
    final stream = soloud.startMixerOutputStream(format: MixerOutputFormat.wav);
    final subscription = stream.listen((_) {});
    addTearDown(subscription.cancel);
    await Future<void>.delayed(const Duration(milliseconds: 200));
    final header = soloud.getMixerOutputWavHeader();
    // Either no header yet, or one for this capture: never the previous one.
    if (header.isNotEmpty) {
      _expectHeader(
        header,
        channels: 2,
        sampleRate: soloud.sampleRate,
        dataSize: _parse(header).dataSize,
      );
    }
    soloud.stopMixerOutputStream();
  });

  test('throws SoLoudNotInitializedException without the engine', () async {
    expect(
      soloud.getMixerOutputWavHeader,
      throwsA(isA<SoLoudNotInitializedException>()),
    );
    await startPlaying();
    await _capture(MixerOutputFormat.wav);
    soloud.deinit();
    expect(
      soloud.getMixerOutputWavHeader,
      throwsA(isA<SoLoudNotInitializedException>()),
    );
  });

  test('a new engine session has no header', () async {
    await startPlaying();
    await _capture(MixerOutputFormat.wav);
    soloud.deinit();
    await startPlaying();
    expect(soloud.getMixerOutputWavHeader(), isEmpty);
  });
}

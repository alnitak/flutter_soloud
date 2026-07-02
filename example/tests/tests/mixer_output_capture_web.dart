import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Web version of the mixer output capture test.
///
/// This is conditionally imported by `tests.dart` when running on the web
/// platform.
Future<OutputBuffer> testMixerOutputCapture() async {
  final output = OutputBuffer();

  await initialize();

  final sound = await SoLoud.instance.loadWaveform(
    WaveForm.square,
    false,
    1,
    0,
  );
  assert(sound.soundHash.isValid, 'Failed to load waveform');
  SoLoud.instance.play(sound, looping: true);
  assert(sound.handles.length == 1, 'play() failed');

  output.writeln(
    'active voices after play: '
    '${SoLoud.instance.getActiveVoiceCount()}',
  );

  // Let the engine produce a few mix buffers.
  await delay(500);
  output.writeln(
    'active voices before capture: '
    '${SoLoud.instance.getActiveVoiceCount()}',
  );

  for (final format in MixerOutputFormat.values) {
    output.writeln('Testing format: $format');

    final chunks = <Uint8List>[];

    final stream = SoLoud.instance.startMixerOutputStream(
      format: format,
    );

    final subscription = stream.listen(
      (chunk) {
        output.writeln(
          'MixerOutputCapture $format chunk received: ${chunk.length}',
        );
        chunks.add(chunk);
      },
      onError: (Object e) => output.writeln('  stream error: $e'),
    );

    // Capture for one second, which is plenty for all formats.
    await delay(1000);
    output.writeln('MixerOutputCapture $format running='
        '${SoLoud.instance.isMixerOutputStreamRunning}');
    // Stop capture before canceling the subscription so the synchronous tail
    // flush is delivered through the stream.
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();

    final totalBytes = chunks.fold<int>(0, (sum, c) => sum + c.length);
    assert(totalBytes > 0, 'No data captured for $format');

    output.writeln('  captured $totalBytes bytes in ${chunks.length} chunks');
    if (chunks.isNotEmpty) {
      final bytesHex =
          chunks.first.take(16).map((b) => b.toRadixString(16)).join(' ');
      final debug =
          '  first chunk length: ${chunks.first.length}, bytes: $bytesHex';
      output.writeln(debug);
    }

    // Compressed formats should be wrapped in their container.
    if (format == MixerOutputFormat.opus) {
      assert(
        chunks.first.length > 27 &&
            chunks.first[0] == 0x4f && // 'O'
            chunks.first[1] == 0x67 && // 'g'
            chunks.first[2] == 0x67 && // 'g'
            chunks.first[3] == 0x53, // 'S'
        'Opus output does not start with OggS magic',
      );
    } else if (format == MixerOutputFormat.vorbis) {
      assert(
        chunks.first.length > 27 &&
            chunks.first[0] == 0x4f && // 'O'
            chunks.first[1] == 0x67 && // 'g'
            chunks.first[2] == 0x67 && // 'g'
            chunks.first[3] == 0x53, // 'S'
        'Vorbis output does not start with OggS magic',
      );
    } else if (format == MixerOutputFormat.flac) {
      assert(
        chunks.first.length > 4 &&
            chunks.first[0] == 0x66 && // 'f'
            chunks.first[1] == 0x4c && // 'L'
            chunks.first[2] == 0x61 && // 'a'
            chunks.first[3] == 0x43, // 'C'
        'FLAC output does not start with fLaC magic',
      );
    } else if (format == MixerOutputFormat.wav) {
      assert(
        chunks.first.length > 4 &&
            chunks.first[0] == 0x52 && // 'R'
            chunks.first[1] == 0x49 && // 'I'
            chunks.first[2] == 0x46 && // 'F'
            chunks.first[3] == 0x46, // 'F'
        'WAV output does not start with RIFF magic',
      );
    }

    await delay(100);
  }

  deinit();

  return output;
}

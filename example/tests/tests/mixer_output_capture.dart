import 'dart:async';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test mixer output capture for PCM and compressed formats.
Future<StringBuffer> testMixerOutputCapture() async {
  final output = StringBuffer();

  await initialize();

  final sound = await loadAsset();
  assert(sound.soundHash.isValid, 'Failed to load asset');
  SoLoud.instance.play(sound, looping: true);
  assert(sound.handles.length == 1, 'play() failed');

  // Let the engine produce a few mix buffers.
  await delay(500);

  for (final format in MixerOutputFormat.values) {
    output.writeln('Testing format: $format');

    final chunks = <Uint8List>[];

    final stream = SoLoud.instance.startMixerOutputStream(
      format: format,
    );

    final subscription = stream.listen(
      chunks.add,
      onError: (Object e) => output.writeln('  stream error: $e'),
    );

    // Capture for one second, which is plenty for all formats.
    await delay(1000);
    await subscription.cancel();
    SoLoud.instance.stopMixerOutputStream();

    final totalBytes = chunks.fold<int>(0, (sum, c) => sum + c.length);
    assert(totalBytes > 0, 'No data captured for $format');

    output.writeln('  captured $totalBytes bytes in ${chunks.length} chunks');

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
    }

    await delay(100);
  }

  deinit();

  return output;
}

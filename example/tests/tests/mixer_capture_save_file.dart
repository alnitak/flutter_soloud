import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Capture mixer output to temporary Opus and Vorbis files for inspection.
///
/// The generated files are written to the system temp directory and their
/// paths are printed to the output so they can be inspected with ffprobe,
/// VLC, or a hex editor.
Future<StringBuffer> testMixerCaptureSaveFile() async {
  final output = StringBuffer();

  await initialize();

  final sound = await loadAsset();
  assert(sound.soundHash.isValid, 'Failed to load asset');
  SoLoud.instance.play(sound, looping: true);
  assert(sound.handles.length == 1, 'play() failed');

  // Let the engine produce a few mix buffers before capturing.
  await delay(500);

  for (final format in [MixerOutputFormat.opus, MixerOutputFormat.vorbis]) {
    output.writeln('Testing format: $format');

    final chunks = <Uint8List>[];
    final stream = SoLoud.instance.startMixerOutputStream(format: format);

    final subscription = stream.listen(
      chunks.add,
      onError: (Object e) => output.writeln('  stream error: $e'),
    );

    // Capture for two seconds so there is enough data to inspect.
    await delay(2000);
    await subscription.cancel();
    SoLoud.instance.stopMixerOutputStream();

    final totalBytes = chunks.fold<int>(0, (sum, c) => sum + c.length);
    assert(totalBytes > 0, 'No data captured for $format');
    output.writeln('  captured $totalBytes bytes in ${chunks.length} chunks');

    final extension = format == MixerOutputFormat.opus ? 'opus' : 'ogg';
    final timestamp = DateTime.now().millisecondsSinceEpoch;
    final fileName = 'mixer_capture_$timestamp.$extension';
    final filePath = '${Directory.systemTemp.path}/$fileName';
    final file = File(filePath);

    final sink = file.openWrite();
    for (final chunk in chunks) {
      sink.add(chunk);
    }
    await sink.close();

    output.writeln('  wrote $filePath (${file.lengthSync()} bytes)');

    // Basic magic number sanity check.
    if (chunks.first.length >= 4) {
      final magic = String.fromCharCodes(chunks.first.sublist(0, 4));
      output.writeln('  first four bytes: $magic');
      assert(
        magic == 'OggS',
        '$format output does not start with OggS magic: $magic',
      );
    }

    await delay(100);
  }

  deinit();

  return output;
}

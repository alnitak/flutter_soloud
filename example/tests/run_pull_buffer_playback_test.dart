import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:path_provider/path_provider.dart';

/// Reproduce the file_stream pull-buffer behavior headlessly and capture the
/// mixer output to a PCM file for analysis.
///
/// Run with: flutter run -d macos tests/run_pull_buffer_playback_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();

  const chunkSize = 10 * 1024;
  const bufferSizeBytes = 5 * 1024 * 1024;
  const bufferTriggerPosition = 0.75;

  const audioAsset = 'assets/audio/sample-MP3.mp3';
  final bytes = (await rootBundle.load(audioAsset)).buffer.asUint8List();
  final totalBytes = bytes.length;

  final fetchedOffsets = <int>{};
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: bufferSizeBytes,
    bufferTriggerPosition: bufferTriggerPosition,
    audioSizeBytes: totalBytes,
    onMoreDataIsNeeded: (offset) {
      if (offset < 0 || offset >= totalBytes) return;
      if (!fetchedOffsets.add(offset)) return;
      final end = (offset + chunkSize).clamp(0, totalBytes);
      final chunk = bytes.sublist(offset, end);
      SoLoud.instance.addPullBufferDataStream(source, chunk, offset: offset);
    },
  );

  final handle = SoLoud.instance.play(source);

  await Future<void>.delayed(const Duration(seconds: 1));

  final dir = await getTemporaryDirectory();
  final path = '${dir.path}/pull_buffer_playback.pcm';
  final fileSink = File(path).openWrite();
  final stream = SoLoud.instance.startMixerOutputStream(
    format: MixerOutputFormat.pcmF32le,
    sampleRate: 44100,
    channels: 2,
  );
  final subscription = stream.listen(fileSink.add);

  await Future<void>.delayed(const Duration(seconds: 4));
  SoLoud.instance.stopMixerOutputStream();
  await subscription.cancel();
  await fileSink.close();

  // ignore: avoid_print
  print('PULL_BUFFER_PLAYBACK_CAPTURED: $path');
  exit(0);
}

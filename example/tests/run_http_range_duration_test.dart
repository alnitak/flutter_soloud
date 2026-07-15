import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:http/http.dart' as http;

/// Headless test that loads the SoundHelix MP3 via HTTP Range requests and
/// verifies that the plugin reports a duration.
///
/// Run with: flutter run -d macos tests/run_http_range_duration_test.dart
void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();

  const url = 'https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3';
  const chunkSize = 64 * 1024;
  const bufferSizeBytes = 5 * 1024 * 1024;
  const bufferTriggerPosition = 0.75;

  final client = http.Client();
  final headResponse = await client.head(Uri.parse(url));
  final totalBytes = int.parse(headResponse.headers['content-length']!);

  final fetchedOffsets = <int>{};
  late final AudioSource source;
  Duration? detectedDuration;

  Future<void> fetchChunk(int offset) async {
    if (offset < 0 || offset >= totalBytes) return;
    if (!fetchedOffsets.add(offset)) return;
    final end = (offset + chunkSize - 1).clamp(0, totalBytes - 1);
    final request = http.Request('GET', Uri.parse(url))
      ..headers['Range'] = 'bytes=$offset-$end';
    final streamedResponse = await client.send(request);
    final bytes = await streamedResponse.stream.toBytes();
    if (bytes.isNotEmpty) {
      SoLoud.instance.addPullBufferDataStream(source, bytes, offset: offset);
    }
  }

  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: bufferSizeBytes,
    bufferTriggerPosition: bufferTriggerPosition,
    audioSizeBytes: totalBytes,
    onAudioDuration: (duration) {
      // ignore: avoid_print
      print('DURATION_DETECTED: $duration seconds');
      detectedDuration = Duration(milliseconds: (duration * 1000).round());
    },
    onMoreDataIsNeeded: fetchChunk,
  );

  final handle = SoLoud.instance.play(source);

  // Wait for the duration to be reported or for playback to start.
  for (var i = 0; i < 50; i++) {
    if (detectedDuration != null) break;
    await Future<void>.delayed(const Duration(milliseconds: 100));
  }

  unawaited(SoLoud.instance.stop(handle));
  SoLoud.instance.deinit();
  client.close();

  if (detectedDuration == null) {
    // ignore: avoid_print
    print('DURATION_TEST_FAILED: duration not detected');
    exit(1);
  }

  // The expected duration is approximately 6:13 (373 seconds).
  const expectedDuration = Duration(seconds: 373);
  final diff = (detectedDuration! - expectedDuration).inSeconds.abs();
  if (diff > 20) {
    // ignore: avoid_print
    print(
      'DURATION_TEST_FAILED: detected=$detectedDuration, '
      'expected=$expectedDuration, diff=$diff seconds',
    );
    exit(1);
  }

  // ignore: avoid_print
  print('DURATION_TEST_PASSED: detectedDuration=$detectedDuration');
  exit(0);
}

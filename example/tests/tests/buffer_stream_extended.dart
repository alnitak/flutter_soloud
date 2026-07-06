import 'dart:math';
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Extended buffer stream testing including reset, time consumed,
/// and buffer size.
Future<OutputBuffer> testBufferStreamExtended() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Test BufferingType.preserved
  strBuf.writeln('Testing BufferingType.preserved');
  final preservedStream = SoLoud.instance.setBufferStream(
    format: BufferType.f32le,
    bufferingTimeNeeds: 1,
    maxBufferSizeBytes: 1024 * 1024 * 5, // 5MB
  );

  // Generate some PCM data
  final random = Random();
  final pcmData = Float32List(44100 * 2); // 1 second at 44.1kHz stereo
  for (var i = 0; i < pcmData.length; i++) {
    pcmData[i] = (random.nextDouble() * 2 - 1) * 0.5;
  }

  // Add data to stream
  SoLoud.instance.addAudioDataStream(
    preservedStream,
    pcmData.buffer.asUint8List(),
  );

  // Check buffer size
  final bufferSize1 = SoLoud.instance.getBufferSize(preservedStream);
  strBuf.writeln('Buffer size after first add: $bufferSize1 bytes');
  assert(bufferSize1 > 0, 'Buffer size should be positive');

  // Play the stream
  SoLoud.instance.play(preservedStream);
  strBuf.writeln('Started playback of preserved stream');

  await delay(500);

  // Add more data
  SoLoud.instance.addAudioDataStream(
    preservedStream,
    pcmData.buffer.asUint8List(),
  );

  final bufferSize2 = SoLoud.instance.getBufferSize(preservedStream);
  strBuf.writeln('Buffer size after second add: $bufferSize2 bytes');
  assert(bufferSize2 > bufferSize1, 'Buffer should have grown');

  await delay(500);

  // Test resetBufferStream
  SoLoud.instance.resetBufferStream(preservedStream);
  strBuf.writeln('Reset buffer stream');

  final bufferSizeAfterReset = SoLoud.instance.getBufferSize(preservedStream);
  strBuf.writeln('Buffer size after reset: $bufferSizeAfterReset bytes');

  // Add data again after reset
  SoLoud.instance.addAudioDataStream(
    preservedStream,
    pcmData.buffer.asUint8List(),
  );
  SoLoud.instance.setDataIsEnded(preservedStream);

  strBuf.writeln('Added data and setDataIsEnded after reset');

  await delay(1000);

  // Test BufferingType.released
  strBuf.writeln('\nTesting BufferingType.released');
  final releasedStream = SoLoud.instance.setBufferStream(
    format: BufferType.f32le,
    bufferingTimeNeeds: 0.5,
    bufferingType: BufferingType.released,
    maxBufferSizeBytes: 1024 * 1024 * 2, // 2MB
  );

  // Add data
  SoLoud.instance.addAudioDataStream(
    releasedStream,
    pcmData.buffer.asUint8List(),
  );

  // Play
  SoLoud.instance.play(releasedStream);
  strBuf.writeln('Started playback of released stream');

  await delay(300);

  // Get time consumed (position in released mode)
  final timeConsumed = SoLoud.instance.getStreamTimeConsumed(releasedStream);
  strBuf.writeln('Stream time consumed: ${timeConsumed.inMilliseconds}ms');
  assert(
    timeConsumed.inMilliseconds > 0,
    'Time consumed should be positive',
  );

  // Add more data while playing
  await delay(200);
  SoLoud.instance.addAudioDataStream(
    releasedStream,
    pcmData.buffer.asUint8List(),
  );

  await delay(500);

  // Mark end
  SoLoud.instance.setDataIsEnded(releasedStream);
  strBuf.writeln('Set data is ended for released stream');

  await delay(1000);

  // Cleanup
  await SoLoud.instance.disposeSource(preservedStream);
  await SoLoud.instance.disposeSource(releasedStream);

  deinit();

  strBuf.writeln('Buffer stream extended tests completed successfully');
  return strBuf;
}

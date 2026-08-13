import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test loading audio from memory buffers.
Future<OutputBuffer> testLoadMem() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load asset bytes
  final byteData = await rootBundle.load('assets/audio/explosion.mp3');
  final bytes = byteData.buffer.asUint8List();
  strBuf.writeln('Loaded ${bytes.length} bytes from asset');

  // Test loadMem with memory mode
  final soundMem = await SoLoud.instance.loadMem(
    'test_explosion_mem',
    bytes,
  );
  strBuf.writeln('Sound loaded from memory (LoadMode.memory)');

  // Get duration to verify it loaded correctly
  final duration = SoLoud.instance.getLength(soundMem);
  strBuf.writeln('Duration: ${duration.inMilliseconds}ms');
  assert(
    duration.inMilliseconds > 0,
    'Loaded sound should have positive duration',
  );

  // Play the sound
  final handle = SoLoud.instance.play(soundMem);
  strBuf.writeln('Playing sound from memory');

  await delay(200);

  // Verify handle is valid
  assert(
    SoLoud.instance.getIsValidVoiceHandle(handle),
    'Voice handle should be valid',
  );

  await delay(500);

  // Stop and dispose
  await SoLoud.instance.stop(handle);
  await SoLoud.instance.disposeSource(soundMem);
  strBuf.writeln('Sound disposed');

  // Test that loading the same reference name again works
  final soundMem2 = await SoLoud.instance.loadMem(
    'test_explosion_mem_2',
    bytes,
  );
  strBuf.writeln('Second sound loaded from memory');

  SoLoud.instance.play(soundMem2);
  await delay(200);
  await SoLoud.instance.disposeSource(soundMem2);

  deinit();

  strBuf.writeln('LoadMem tests completed successfully');
  return strBuf;
}

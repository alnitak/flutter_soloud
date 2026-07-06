import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test reading audio samples from file and memory.
Future<OutputBuffer> testReadSamples() async {
  final strBuf = OutputBuffer();
  await initialize();

  // Load asset bytes for memory reading
  final byteData = await rootBundle.load('assets/audio/explosion.mp3');
  final bytes = byteData.buffer.asUint8List();

  // Test readSamplesFromMem
  strBuf.writeln('Reading 1024 samples from memory (averaged)');
  final samplesAvg = await SoLoud.instance.readSamplesFromMem(
    bytes,
    1024,
    average: true,
  );
  strBuf.writeln('Got ${samplesAvg.length} samples (averaged)');
  assert(samplesAvg.length == 1024, 'Should have 1024 averaged samples');

  // Test without averaging
  strBuf.writeln('Reading 1024 samples from memory (not averaged)');
  final samplesRaw = await SoLoud.instance.readSamplesFromMem(
    bytes,
    1024,
    // ignore: avoid_redundant_argument_values
    average: false,
  );
  strBuf.writeln('Got ${samplesRaw.length} samples (raw)');
  assert(samplesRaw.length == 1024, 'Should have 1024 raw samples');

  // Verify sample values are in valid range [-1.0, 1.0]
  var minVal = double.infinity;
  var maxVal = double.negativeInfinity;
  for (final sample in samplesAvg) {
    if (sample < minVal) minVal = sample;
    if (sample > maxVal) maxVal = sample;
  }
  strBuf.writeln('Sample range: $minVal to $maxVal');
  assert(
    minVal >= -1.0 && maxVal <= 1.0,
    'Samples should be in range [-1.0, 1.0]',
  );

  // Test reading different sample counts
  strBuf.writeln('Reading 512 samples from memory');
  final samples512 = await SoLoud.instance.readSamplesFromMem(
    bytes,
    512,
  );
  assert(samples512.length == 512, 'Should have 512 samples');

  strBuf.writeln('Reading 2048 samples from memory');
  final samples2048 = await SoLoud.instance.readSamplesFromMem(
    bytes,
    2048,
  );
  assert(samples2048.length == 2048, 'Should have 2048 samples');

  // Note: readSamplesFromFile is not available on web
  strBuf.writeln('readSamplesFromFile not tested (not available on web)');

  deinit();

  strBuf.writeln('Read samples tests completed successfully');
  return strBuf;
}

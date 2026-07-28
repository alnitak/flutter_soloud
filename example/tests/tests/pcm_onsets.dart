import 'dart:typed_data';

/// Helpers to accumulate mixer output PCM in memory and analyze it.
///
/// Used by the `playClocked` and `playScheduled` tests: instead of saving
/// the captured mixer output to a file, the `pcmF32le` chunks are kept in
/// memory and analyzed once the capture ends.

/// Accumulates `pcmF32le` mono chunks into a growable list of samples.
class PcmAccumulator {
  final samples = <double>[];

  void addChunk(Uint8List chunk) {
    final floats = Float32List.view(
      chunk.buffer,
      chunk.offsetInBytes,
      chunk.lengthInBytes ~/ 4,
    );
    samples.addAll(floats);
  }
}

/// Detect note onsets in mono float PCM: a sample whose absolute value
/// crosses [threshold] after at least [quietSamples] consecutive samples
/// below it. Returns the onset times in seconds.
List<double> detectOnsets(
  List<double> samples, {
  double threshold = 0.1,
  int quietSamples = 441, // 10 ms at 44100 Hz
  int sampleRate = 44100,
}) {
  final onsets = <double>[];
  var quietRun = quietSamples;
  for (var i = 0; i < samples.length; i++) {
    if (samples[i].abs() >= threshold) {
      if (quietRun >= quietSamples) {
        onsets.add(i / sampleRate);
      }
      quietRun = 0;
    } else {
      quietRun++;
    }
  }
  return onsets;
}

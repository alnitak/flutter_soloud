// Captures the master mixer output of the real engine (see
// `SoLoud.startMixerOutputStream`) so host tests can assert on what was
// actually mixed instead of on timing.

import 'dart:async';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';

/// Interleaved float samples captured from the mixer output.
class CapturedOutput {
  /// Wraps interleaved [samples] of [channels] channels.
  CapturedOutput(this.samples, this.channels);

  /// Interleaved samples, in -1..1.
  final Float32List samples;

  /// Number of interleaved channels.
  final int channels;

  /// Number of frames captured.
  int get frames => samples.length ~/ channels;

  /// The samples of [channel] alone.
  Float32List channel(int channel) {
    final out = Float32List(frames);
    for (var f = 0; f < frames; f++) {
      out[f] = samples[f * channels + channel];
    }
    return out;
  }

  /// Root mean square of [channel].
  double rms(int channel) {
    var sum = 0.0;
    for (var f = 0; f < frames; f++) {
      final s = samples[f * channels + channel];
      sum += s * s;
    }
    return frames == 0 ? 0 : math.sqrt(sum / frames);
  }

  /// Largest absolute sample of [channel].
  double peak(int channel) {
    var p = 0.0;
    for (var f = 0; f < frames; f++) {
      p = math.max(p, samples[f * channels + channel].abs());
    }
    return p;
  }

  /// Frequency of [channel] in Hz estimated from its rising zero crossings
  /// (good for a single steady tone), at [sampleRate].
  double frequency(int channel, {int sampleRate = 44100}) {
    final data = this.channel(channel);
    int? first;
    int? last;
    var crossings = 0;
    for (var i = 1; i < data.length; i++) {
      if (data[i - 1] < 0 && data[i] >= 0) {
        // Linear interpolation is not worth it over thousands of frames.
        first ??= i;
        last = i;
        crossings++;
      }
    }
    if (first == null || last == null || crossings < 2) return 0;
    return (crossings - 1) * sampleRate / (last - first);
  }
}

/// Captures [frames] frames of the master output (PCM f32le, at the engine
/// rate and channel count), after dropping the first [skipFrames] frames so
/// that what was mixed before the call does not count.
///
/// The engine must be initialized with the default stereo output and
/// something must be playing (the device stops producing when idle).
Future<CapturedOutput> captureOutput({
  int frames = 8192,
  int skipFrames = 2048,
  int channels = 2,
  Duration timeout = const Duration(seconds: 5),
}) async {
  final soloud = SoLoud.instance;
  final wanted = (frames + skipFrames) * channels * 4;
  final builder = BytesBuilder(copy: false);
  final done = Completer<void>();
  final subscription = soloud
      .startMixerOutputStream(chunkPCMFrames: 2048)
      .listen(
        (chunk) {
          builder.add(chunk);
          if (builder.length >= wanted && !done.isCompleted) done.complete();
        },
        onDone: () {
          if (!done.isCompleted) done.complete();
        },
      );
  try {
    await done.future.timeout(timeout);
  } finally {
    soloud.stopMixerOutputStream();
    await subscription.cancel();
  }
  final bytes = builder.takeBytes();
  final skipBytes = math.min(bytes.length, skipFrames * channels * 4);
  final usable = math.max(0, math.min(bytes.length, wanted) - skipBytes);
  final copy = Uint8List.fromList(bytes.sublist(skipBytes, skipBytes + usable));
  return CapturedOutput(copy.buffer.asFloat32List(), channels);
}

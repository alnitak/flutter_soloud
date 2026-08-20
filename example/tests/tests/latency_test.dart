import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

// ignore_for_file: experimental_member_use

/// Latency test for retroactive re-mixing when the engine is initialized with
/// a large buffer size (8192 frames) and using [SoLoud.playScheduled].
///
/// In this configuration:
/// - `bufferSize`: 8192 frames (~185.8 ms at 44.1 kHz)
/// - `renderAheadFrames`: 8192 frames
/// - `devicePeriodFrames`: 512 frames (~11.6 ms at 44.1 kHz)
/// - Sound source: `assets/audio/tic-1.wav` loaded into a [BufferStream] with
///   [BufferingType.preserved].
///
/// With retroactive re-mixing, calling `playScheduled(sound, Duration.zero)`
/// plays immediately in the current audio buffer window at the playhead
/// (bounded by `devicePeriodFrames`), rather than waiting for the entire
/// 8192-frame buffer window to complete.
///
/// The test captures the output in memory via [SoLoud.startMixerOutputStream],
/// and validates the peak timings directly in memory.
Future<OutputBuffer> testLatency() async {
  final output = OutputBuffer();

  const bufferSize = 8192;
  const renderAhead = 8192;
  const devicePeriod = 512;
  const sampleRate = 44100;
  const playCount = 30;
  const intervalMs =
      60; // 60 ms spacing: multiple plays fall inside one 185.8 ms window

  output.writeln('Initializing SoLoud engine with bufferSize=$bufferSize, '
      'renderAheadFrames=$renderAhead, devicePeriodFrames=$devicePeriod');

  await SoLoud.instance.init(
    bufferSize: bufferSize,
    renderAheadFrames: renderAhead,
    devicePeriodFrames: devicePeriod,
    channels: Channels.mono,
    sampleRate: sampleRate,
  );
  SoLoud.instance.setMaxActiveVoiceCount(64);

  try {
    // 1. Load tic-1.wav into a BufferStream with BufferingType.preserved
    output
        .writeln('Loading assets/audio/tic-1.wav into preserved BufferStream');
    final wavBytes =
        (await rootBundle.load('assets/audio/tic-1.wav')).buffer.asUint8List();
    assert(wavBytes.isNotEmpty, 'Failed to load tic-1.wav from assets');

    final sound = SoLoud.instance.setBufferStream(
      format: BufferType.auto,
      bufferingType: BufferingType.preserved,
      bufferingTimeNeeds: 0,
      maxBufferSizeBytes: 1024 * 1024,
    );
    assert(sound.soundHash.isValid, 'Failed to create BufferStream');

    SoLoud.instance.addAudioDataStream(sound, wavBytes);

    // Let the buffer stream finish decoding
    await delay(100);

    // 2. Start mixer output capture
    final pcmChunks = <Uint8List>[];
    final stream = SoLoud.instance.startMixerOutputStream(
      format: MixerOutputFormat.pcmF32le,
      channels: 1,
      sampleRate: sampleRate,
    );
    final subscription = stream.listen(
      pcmChunks.add,
      onError: (Object e) => output.writeln('Mixer stream error: $e'),
    );

    // Allow capture stream to stabilize
    await delay(300);

    // 3. Play the sound multiple times with playScheduled(sound, Duration.zero)
    output
        .writeln('Playing $playCount sounds spaced $intervalMs ms apart using '
            'playScheduled(sound, Duration.zero)...');

    final triggerTimes = <int>[];
    final stopwatch = Stopwatch()..start();

    for (var i = 0; i < playCount; i++) {
      final elapsed = stopwatch.elapsedMilliseconds;
      final engineTime = SoLoud.instance.getEngineTime();
      final handle = SoLoud.instance.playScheduled(sound, Duration.zero);
      output.writeln(
          'Iteration $i: elapsed=${elapsed}ms, engineTime=${engineTime.inMilliseconds}ms, handle=$handle');
      triggerTimes.add(elapsed);
      assert(!handle.isError, 'playScheduled failed on iteration $i');

      if (i < playCount - 1) {
        await delay(intervalMs);
      }
    }

    output.writeln('Trigger times (ms): $triggerTimes');

    // Wait for the final sound to finish playing and be captured
    await delay(500);

    // 4. Stop mixer output capture
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();

    // Combine all chunks into a Float32List in memory
    final totalBytes = pcmChunks.fold<int>(0, (sum, c) => sum + c.length);
    output.writeln('Captured $totalBytes bytes of PCM audio');
    assert(totalBytes > 0, 'No PCM audio captured');

    final combinedPcm = Uint8List(totalBytes);
    var byteOffset = 0;
    for (final chunk in pcmChunks) {
      combinedPcm.setRange(byteOffset, byteOffset + chunk.length, chunk);
      byteOffset += chunk.length;
    }

    final floatSamples = Float32List.view(
      combinedPcm.buffer,
      combinedPcm.offsetInBytes,
      combinedPcm.lengthInBytes ~/ 4,
    );
    final durationSec = floatSamples.length / sampleRate;
    output.writeln(
      'Read ${floatSamples.length} samples (${durationSec.toStringAsFixed(3)} s) from in-memory capture',
    );

    // 5. In-memory peak detection and interval analysis
    final peaks = _detectPeaks(floatSamples, sampleRate: sampleRate);
    output.writeln('Detected ${peaks.length} peak(s):');
    for (var idx = 0; idx < peaks.length; idx++) {
      final p = peaks[idx];
      output.writeln(
        '  Peak #${idx + 1}: sample ${p.sampleIndex} at ${p.timeMs.toStringAsFixed(2)} ms (amp=${p.amplitude.toStringAsFixed(3)})',
      );
    }

    assert(
      peaks.length >= playCount,
      'Detected ${peaks.length} peaks, but expected at least $playCount',
    );

    // Evaluate intervals between the first playCount peaks
    final firstPeaks = peaks.sublist(0, playCount);
    final intervalsMs = <double>[];
    final deviationsMs = <double>[];
    const maxAllowedDeviationMs = 20.0;

    output.writeln('\nEvaluating first $playCount peak intervals against trigger times:');
    for (var idx = 1; idx < firstPeaks.length; idx++) {
      final interval = firstPeaks[idx].timeMs - firstPeaks[idx - 1].timeMs;
      final expectedInterval =
          (triggerTimes[idx] - triggerTimes[idx - 1]).toDouble();
      final deviation = (interval - expectedInterval).abs();
      intervalsMs.add(interval);
      deviationsMs.add(deviation);
      output.writeln(
        '  Gap $idx -> ${idx + 1}: ${interval.toStringAsFixed(2)} ms '
        '(trigger delta: ${expectedInterval.toStringAsFixed(1)} ms, deviation: ${deviation.toStringAsFixed(2)} ms)',
      );
    }

    final maxDev = deviationsMs.isNotEmpty
        ? deviationsMs.reduce((a, b) => a > b ? a : b)
        : 0.0;
    final avgInterval = intervalsMs.isNotEmpty
        ? intervalsMs.reduce((a, b) => a + b) / intervalsMs.length
        : 0.0;

    output.writeln('\nSummary:');
    output.writeln('  Average interval: ${avgInterval.toStringAsFixed(2)} ms');
    output.writeln(
      '  Maximum deviation: ${maxDev.toStringAsFixed(2)} ms (threshold: ${maxAllowedDeviationMs.toStringAsFixed(2)} ms)',
    );

    assert(
      maxDev <= maxAllowedDeviationMs,
      'Maximum interval deviation (${maxDev.toStringAsFixed(2)} ms) exceeds tolerance '
      '(${maxAllowedDeviationMs.toStringAsFixed(2)} ms). Retroactive re-mixing latency test failed!',
    );

    output.writeln('SUCCESS: All peak intervals within expected low-latency bounds!');
    // ignore: avoid_print
    print(output);

    // Cleanup
    await SoLoud.instance.disposeSource(sound);
    deinit();
    output.writeln('Latency test PASSED!');
    return output;
  } catch (e) {
    deinit();
    rethrow;
  }
}

/// Represents a detected audio pulse peak in the PCM capture.
class _DetectedPeak {
  final int sampleIndex;
  final double amplitude;
  final double timeMs;

  const _DetectedPeak({
    required this.sampleIndex,
    required this.amplitude,
    required this.timeMs,
  });
}

/// Detects sound pulse peaks in a mono Float32List audio buffer in memory.
List<_DetectedPeak> _detectPeaks(
  Float32List samples, {
  int sampleRate = 44100,
  double threshold = 0.15,
  int minDistanceSamples = 1000,
  int localWindowSamples = 500,
}) {
  final peaks = <_DetectedPeak>[];
  var i = 0;
  final n = samples.length;

  while (i < n) {
    final sample = samples[i].abs();
    if (sample >= threshold) {
      final windowEnd = (i + localWindowSamples < n) ? i + localWindowSamples : n;
      var maxIdx = i;
      var maxVal = sample;
      for (var j = i; j < windowEnd; j++) {
        final val = samples[j].abs();
        if (val > maxVal) {
          maxVal = val;
          maxIdx = j;
        }
      }
      peaks.add(
        _DetectedPeak(
          sampleIndex: maxIdx,
          amplitude: maxVal,
          timeMs: (maxIdx / sampleRate) * 1000,
        ),
      );
      i = maxIdx + minDistanceSamples;
    } else {
      i++;
    }
  }

  return peaks;
}

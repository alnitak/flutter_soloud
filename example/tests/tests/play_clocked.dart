import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';
import 'pcm_onsets.dart';

/// Accuracy test for [SoLoud.playClocked] with in-memory mixer capture.
///
/// The engine runs with a 4096-sample buffer (~93 ms at 44100 Hz), so plain
/// `play()` would quantize the ticks to ~93 ms boundaries. A metronome is
/// run with tick delays of 50, 100 and 200 ms using `playClocked` while the
/// mixer output is captured as `pcmF32le` (mono) into memory. The tick
/// onsets are then detected and their spacings asserted against the
/// requested delays.
Future<OutputBuffer> testPlayClocked() async {
  final output = OutputBuffer();

  const bufferSize = 4096;

  await SoLoud.instance.init(bufferSize: bufferSize, channels: Channels.mono);

  final tick = await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  assert(tick.soundHash.isValid, 'Failed to load tick');

  const delaysMs = [50, 100, 200];
  const ticksPerRun = 10;

  for (final delayMs in delaysMs) {
    // Each run is a new scheduling session with its own time base: reset
    // the clocked-play anchor so the run re-anchors to its own clock.
    SoLoud.instance.resetStreamTime();

    final pcm = PcmAccumulator();
    // ignore: experimental_member_use
    final stream = SoLoud.instance.startMixerOutputStream(
      // ignore: avoid_redundant_argument_values
      format: MixerOutputFormat.pcmF32le,
      channels: 1,
    );
    final subscription = stream.listen(pcm.addChunk);

    // Let the capture stream start cleanly before scheduling ticks.
    await delay(300);

    // Metronome: schedule the ticks with playClocked using an accumulated
    // ideal "physics time".
    var physicsTime = Duration.zero;
    final tickDelay = Duration(milliseconds: delayMs);
    final done = Completer<void>();
    var count = 0;
    Timer.periodic(tickDelay, (timer) {
      physicsTime += tickDelay;
      SoLoud.instance.playClocked(tick, physicsTime);
      count++;
      if (count >= ticksPerRun) {
        timer.cancel();
        done.complete();
      }
    });
    await done.future;

    // Wait for the last tick to be played and captured.
    await delay(600);

    // ignore: experimental_member_use
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();

    final onsets = detectOnsets(pcm.samples);
    output.writeln(
      'delay=${delayMs}ms onsets=${onsets.length} '
      'at [${onsets.map((t) => t.toStringAsFixed(3)).join(', ')}]',
    );
    assert(
      onsets.length == ticksPerRun,
      'detected ${onsets.length} ticks, expected $ticksPerRun',
    );

    var maxDeviationMs = 0.0;
    for (var n = 1; n < onsets.length; n++) {
      final deviationMs = ((onsets[n] - onsets[n - 1]) * 1000 - delayMs).abs();
      if (deviationMs > maxDeviationMs) maxDeviationMs = deviationMs;
    }
    output.writeln(
      'delay=${delayMs}ms max spacing deviation: '
      '${maxDeviationMs.toStringAsFixed(2)} ms',
    );
    assert(
      maxDeviationMs <= 10,
      'spacing deviation ${maxDeviationMs}ms exceeds 10ms at delay $delayMs',
    );

    await delay(300);
  }

  deinit();

  output.writeln('playClocked accuracy OK');
  return output;
}

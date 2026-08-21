import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';
import 'pcm_onsets.dart';

/// Accuracy test for [SoLoud.playScheduled], [SoLoud.stopScheduled] and
/// [SoLoud.fadeScheduled] against [SoLoud.getEngineTime], with in-memory
/// mixer capture.
///
/// Part A captures the mixer output as `pcmF32le` (mono) into memory while
/// 8 ticks are scheduled 100 ms apart starting 500 ms out, plus one tick 3
/// seconds out (beyond the ~2 s re-anchor guard of `playClocked`). It also
/// verifies the sample-accurate cutoff: the tick source has ~11 ms of
/// leading silence and peaks at 16 ms, so a 12 ms duration must be silent
/// while a 25 ms duration must be audible (a buffer-quantized stop would
/// play both fully).
///
/// Part B runs state checks: a scheduled play with `duration`, an absolute
/// `stopScheduled`, cancelling a still-pending scheduled sound with
/// `stop`, and a `fadeScheduled` with `thenStop`.
Future<OutputBuffer> testPlayScheduled() async {
  final output = OutputBuffer();

  // This test waits on engine time after scheduled voices have ended.
  // Keep the audio device running so the engine clock continues advancing.
  SoLoud.instance.setAudioDeviceIdleTimeout(null);

  try {
    const bufferSize = 1024;
    const renderAheadFrames = 1024;
    const devicePeriodFrames = 128;

    await SoLoud.instance.init(
      bufferSize: bufferSize,
      renderAheadFrames: renderAheadFrames,
      devicePeriodFrames: devicePeriodFrames,
      channels: Channels.mono,
    );

    // ---------------------------------------------------------------- Part A
    final tick = await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
    assert(tick.soundHash.isValid, 'Failed to load tick');

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

    // Schedule the batch against the engine clock: 8 ticks 100 ms apart
    // starting 500 ms out, one tick 3 seconds out, and the cutoff pair.
    final now = SoLoud.instance.getEngineTime();
    const ticksStartMs = 500;
    const tickSpacingMs = 100;
    const tickCount = 8;
    const farTickMs = 3000;
    const silentCutMs = farTickMs + 600;
    const audibleCutMs = farTickMs + 900;
    for (var n = 0; n < tickCount; n++) {
      SoLoud.instance.playScheduled(
        tick,
        now + Duration(milliseconds: ticksStartMs + tickSpacingMs * n),
      );
    }
    SoLoud.instance.playScheduled(
      tick,
      now + const Duration(milliseconds: farTickMs),
    );
    SoLoud.instance.playScheduled(
      tick,
      now + const Duration(milliseconds: silentCutMs),
      duration: const Duration(milliseconds: 12),
    );
    SoLoud.instance.playScheduled(
      tick,
      now + const Duration(milliseconds: audibleCutMs),
      duration: const Duration(milliseconds: 25),
    );

    // Wait for the last tick to be played and captured.
    await delay(audibleCutMs + 800);

    // ignore: experimental_member_use
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();

    final onsets = detectOnsets(pcm.samples);
    output.writeln(
      'onsets=${onsets.length} '
      'at [${onsets.map((t) => t.toStringAsFixed(3)).join(', ')}]',
    );

    // The 12 ms duration tick must be silent (cut before the peak), so only
    // the 8 ticks + far tick + 25 ms duration tick produce onsets.
    const expectedOnsets = tickCount + 2;
    assert(
      onsets.length == expectedOnsets,
      'detected ${onsets.length} onsets, expected $expectedOnsets '
      '(12 ms duration tick should be silent)',
    );

    var maxDeviationMs = 0.0;
    for (var n = 1; n < tickCount; n++) {
      final deviationMs =
          ((onsets[n] - onsets[n - 1]) * 1000 - tickSpacingMs).abs();
      if (deviationMs > maxDeviationMs) maxDeviationMs = deviationMs;
    }
    output.writeln(
      'tick spacings are $tickSpacingMs ms '
      '(max deviation: ${maxDeviationMs.toStringAsFixed(2)} ms)',
    );
    assert(maxDeviationMs <= 10, 'spacing deviation exceeds 10 ms');

    final farGapMs = (onsets[tickCount] - onsets[tickCount - 1]) * 1000;
    const expectedFarGapMs = farTickMs - (ticksStartMs + tickSpacingMs * 7);
    output.writeln(
      'far tick gap: ${farGapMs.toStringAsFixed(2)} ms '
      '(expected $expectedFarGapMs ms)',
    );
    assert(
      (farGapMs - expectedFarGapMs).abs() <= 50,
      'far tick gap deviates by more than 50 ms (re-anchor guard hit?)',
    );

    final cutGapMs = (onsets[tickCount + 1] - onsets[tickCount]) * 1000;
    const expectedCutGapMs = audibleCutMs - farTickMs;
    output.writeln(
      '25 ms duration tick gap: ${cutGapMs.toStringAsFixed(2)} ms '
      '(expected $expectedCutGapMs ms)',
    );
    assert(
      (cutGapMs - expectedCutGapMs).abs() <= 50,
      'duration tick gap deviates by more than 50 ms',
    );

    // ---------------------------------------------------------------- Part B
    final music = await SoLoud.instance.loadAsset(
      'assets/audio/8_bit_mentality.mp3',
    );
    assert(music.soundHash.isValid, 'Failed to load music');

    Future<void> waitEngineTime(Duration t) async {
      while (SoLoud.instance.getEngineTime() < t) {
        await delay(5);
      }
    }

    // 1. playScheduled with duration stops by itself.
    var t0 = SoLoud.instance.getEngineTime();
    final h1 = SoLoud.instance.playScheduled(
      music,
      t0 + const Duration(milliseconds: 300),
      duration: const Duration(milliseconds: 600),
    );
    await waitEngineTime(t0 + const Duration(milliseconds: 1500));
    assert(
      !SoLoud.instance.getIsValidVoiceHandle(h1),
      'playScheduled with duration did not stop the sound',
    );
    output.writeln('playScheduled with duration stopped the sound');

    // 2. stopScheduled cuts a sound pinned to the engine clock.
    t0 = SoLoud.instance.getEngineTime();
    final h2 = SoLoud.instance.playScheduled(
      music,
      t0 + const Duration(milliseconds: 300),
    );
    SoLoud.instance.stopScheduled(h2, t0 + const Duration(milliseconds: 900));
    await waitEngineTime(t0 + const Duration(milliseconds: 1500));
    assert(
      !SoLoud.instance.getIsValidVoiceHandle(h2),
      'stopScheduled did not stop the sound',
    );
    output.writeln('stopScheduled stopped the sound at the scheduled time');

    // 3. A still-pending scheduled sound can be cancelled with stop.
    t0 = SoLoud.instance.getEngineTime();
    final h3 = SoLoud.instance.playScheduled(
      music,
      t0 + const Duration(seconds: 2),
    );
    unawaited(SoLoud.instance.stop(h3));
    assert(
      !SoLoud.instance.getIsValidVoiceHandle(h3),
      'pending scheduled sound was not cancelled',
    );
    output.writeln('pending scheduled sound cancelled with stop');

    // 4. fadeScheduled with thenStop stops the sound when the fade ends.
    t0 = SoLoud.instance.getEngineTime();
    final h4 = SoLoud.instance.playScheduled(
      music,
      t0 + const Duration(milliseconds: 300),
    );
    SoLoud.instance.fadeScheduled(
      h4,
      t0 + const Duration(milliseconds: 500),
      0,
      const Duration(milliseconds: 400),
      thenStop: true,
    );
    await waitEngineTime(t0 + const Duration(milliseconds: 1600));
    assert(
      !SoLoud.instance.getIsValidVoiceHandle(h4),
      'fadeScheduled with thenStop did not stop the sound',
    );
    output.writeln('fadeScheduled with thenStop stopped the sound');

    deinit();

    output.writeln('playScheduled accuracy OK');
    return output;
  } finally {
    // Restore default audio device idle timeout
    SoLoud.instance.setAudioDeviceIdleTimeout(
      const Duration(milliseconds: 500),
    );
  }
}

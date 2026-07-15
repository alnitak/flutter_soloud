import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test looping state and a bounded `[loopingStartAt, loopingEndAt)` region.
Future<OutputBuffer> loopingTests() async {
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  final handle = SoLoud.instance.play(
    currentSound,
    looping: true,
    loopingStartAt: const Duration(seconds: 1),
    loopingEndAt: const Duration(seconds: 2),
  );
  assert(
    SoLoud.instance.getLooping(handle),
    'looping failed!',
  );
  assert(
    SoLoud.instance.getLoopEndPoint(handle) == const Duration(seconds: 2),
    'looping end failed!',
  );

  /// Wait for the first pass and at least one bounded loop.
  await delay(3500);
  final position = SoLoud.instance.getPosition(handle);
  assert(
    SoLoud.instance.getLoopPoint(handle) == const Duration(seconds: 1) &&
        position >= const Duration(seconds: 1) &&
        position < const Duration(seconds: 2),
    'bounded looping failed at $position!',
  );

  SoLoud.instance.setLoopEndPoint(handle, null);
  assert(
    SoLoud.instance.getLoopEndPoint(handle) == null,
    'clearing the looping end failed!',
  );

  deinit();
  return OutputBuffer();
}

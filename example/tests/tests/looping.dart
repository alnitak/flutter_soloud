import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test looping state and `loopingStartAt`.
Future<OutputBuffer> loopingTests() async {
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  SoLoud.instance.play(
    currentSound,
    looping: true,
    loopingStartAt: const Duration(seconds: 1),
  );
  assert(
    SoLoud.instance.getLooping(currentSound.handles.first),
    'looping failed!',
  );

  /// Wait for the first loop to start at 1s
  await delay(4100);
  assert(
    SoLoud.instance.getLoopPoint(currentSound.handles.first) ==
            const Duration(seconds: 1) &&
        SoLoud.instance.getPosition(currentSound.handles.first) >
            const Duration(seconds: 1),
    'looping start failed!',
  );

  deinit();
  return OutputBuffer();
}

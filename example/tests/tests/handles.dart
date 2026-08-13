import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test instancing playing handles and their disposal.
Future<OutputBuffer> testHandles() async {
  var output = '';

  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  currentSound.soundEvents.listen((event) {
    if (event.event == SoundEventType.handleIsNoMoreValid) {
      output = 'SoundEvent.handleIsNoMoreValid';
    }
    if (event.event == SoundEventType.soundDisposed) {
      output = 'SoundEvent.soundDisposed';
    }
  });

  /// Play sample
  SoLoud.instance.play(currentSound);
  assert(
    currentSound.soundHash.isValid && currentSound.handles.length == 1,
    'play() failed!',
  );

  /// 3798ms explosion.mp3 sample duration
  await delay(4500);
  assert(
    output == 'SoundEvent.handleIsNoMoreValid',
    'Sound end playback event not triggered!',
  );

  /// Play 4 sample
  SoLoud.instance.play(currentSound);
  SoLoud.instance.play(currentSound);
  SoLoud.instance.play(currentSound);
  SoLoud.instance.play(currentSound);
  assert(
    currentSound.handles.length == 4,
    'loadFromAssets() failed!',
  );

  /// Wait for the sample to finish and see in log:
  /// "SoundEvent.handleIsNoMoreValid .* has [3-2-1-0] active handles"
  /// 3798ms explosion.mp3 sample duration
  await delay(4500);
  assert(
    currentSound.handles.isEmpty,
    'Play 4 sample handles failed!',
  );

  deinit();
  return OutputBuffer();
}

import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test setMaxActiveVoiceCount, setProtectedVoice and getProtectedVoice.
Future<OutputBuffer> testProtectVoice() async {
  final strBuf = OutputBuffer();
  await initialize();
  final defaultVoiceCount = SoLoud.instance.getMaxActiveVoiceCount();

  SoLoud.instance.setMaxActiveVoiceCount(6);
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 6,
    "setMaxActiveVoiceCount() didn't work properly",
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    SoLoud.instance.play(explosion);
    await delay(100);
  }

  /// play 1 protected [song]
  final songHandle = SoLoud.instance.play(song);
  SoLoud.instance.setProtectVoice(songHandle, true);
  assert(
    SoLoud.instance.getProtectVoice(songHandle),
    "setProtectVoice() didn't work properly",
  );

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    SoLoud.instance.play(explosion);
    await delay(100);
  }

  await delay(1000);

  assert(
    SoLoud.instance.getIsValidVoiceHandle(songHandle) &&
        SoLoud.instance.getActiveVoiceCount() == 6,
    'The protected song has been stopped!',
  );

  // Test getVoiceCount (total voices allocated)
  final voiceCount = SoLoud.instance.getVoiceCount();
  strBuf.writeln('Total voice count: $voiceCount');
  assert(voiceCount >= 6, 'Voice count should be at least 6');

  // Test setInaudibleBehavior - positional args: handle, mustTick, kill
  // This ensures voices continue to "tick" even when inaudible
  SoLoud.instance.setInaudibleBehavior(songHandle, true, false);
  strBuf.writeln('Set inaudible behavior for protected voice');

  deinit();

  /// Afer disposing the player and re-initializing, max active voices
  /// should be reset to 16
  await initialize();
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == defaultVoiceCount,
    'Max active voices are not reset to the default value after reinit!',
  );
  deinit();

  return strBuf;
}

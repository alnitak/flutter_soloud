import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test setMaxActiveVoiceCount.
Future<OutputBuffer> testMaxVoices() async {
  final strBuf = OutputBuffer();
  await initialize();

  var explosion = await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  SoLoud.instance.setGlobalVolume(0.02);

  /// 1. Test withh 10 voices
  SoLoud.instance.setMaxActiveVoiceCount(10);

  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 10,
    "setMaxActiveVoiceCount() didn't work properly",
  );

  for (var i = 0; i < 15; i++) {
    SoLoud.instance.play(explosion);
    await delay(5);
  }

  assert(
    SoLoud.instance.getActiveVoiceCount() == 10,
    'Active voice count should be 10',
  );

  strBuf.writeln('Played 15 explosions with max 10 voices, Ok');

  await SoLoud.instance.disposeAllSources();
  await delay(100);

  /// 2. Test withh 100 voices
  // Reload the sound after disposing all sources
  explosion = await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  SoLoud.instance.setMaxActiveVoiceCount(100);

  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 100,
    "setMaxActiveVoiceCount() didn't work properly",
  );

  /// play 110 explosion
  for (var i = 0; i < 110; i++) {
    SoLoud.instance.play(explosion);
    await delay(5);
  }

  assert(
    SoLoud.instance.getActiveVoiceCount() == 100,
    'Active voice count should be 100',
  );
  await SoLoud.instance.disposeAllSources();
  await delay(100);

  strBuf.writeln('Played 110 explosions with max 100 voices, Ok');

  /// 3. Test withh 1000 voices
  // Reload the sound after disposing all sources
  explosion = await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');
  SoLoud.instance.setMaxActiveVoiceCount(1000);

  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 1000,
    "setMaxActiveVoiceCount() didn't work properly",
  );

  /// play 1020 explosion
  for (var i = 0; i < 1020; i++) {
    SoLoud.instance.play(explosion);
    await delay(5);
  }

  assert(
    SoLoud.instance.getActiveVoiceCount() == 1000,
    'Active voice count should be 1000',
  );
  await SoLoud.instance.disposeAllSources();
  await delay(100);

  strBuf.writeln('Played 1020 explosions with max 1000 voices, Ok');

  deinit();

  return strBuf;
}

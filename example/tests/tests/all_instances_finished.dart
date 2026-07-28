import 'dart:async';

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test allInstancesFinished stream.
Future<OutputBuffer> testAllInstancesFinished() async {
  final strBuf = OutputBuffer();
  await initialize();

  await SoLoud.instance.disposeAllSources();
  assert(
    SoLoud.instance.activeSounds.isEmpty,
    'Active sounds even after disposeAllSound()',
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
    mode: LoadMode.disk,
  );

  // Set up unloading.
  var explosionDisposed = false;
  var songDisposed = false;
  unawaited(
    explosion.allInstancesFinished.first.then((_) async {
      strBuf.write('All instances of explosion finished.\n');
      await SoLoud.instance.disposeSource(explosion);
      explosionDisposed = true;
    }),
  );
  unawaited(
    song.allInstancesFinished.first.then((_) async {
      strBuf.write('All instances of song finished.\n');
      await SoLoud.instance.disposeSource(song);
      songDisposed = true;
    }),
  );

  SoLoud.instance.play(explosion, volume: 0.2);
  final songHandle = SoLoud.instance.play(song, volume: 0.6);
  await delay(500);
  SoLoud.instance.play(explosion, volume: 0.3);

  // Let the second explosion play for its full duration.
  await delay(4000);

  await SoLoud.instance.stop(songHandle);
  await delay(1000);

  assert(explosionDisposed, "Explosion sound wasn't disposed.");
  assert(songDisposed, "Song sound wasn't disposed.");

  deinit();

  return strBuf;
}

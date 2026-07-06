import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

Future<OutputBuffer> testAsyncMultiLoad() async {
  final strBuf = OutputBuffer();
  await initialize();

  final sounds = [
    SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3'),
    SoLoud.instance.loadAsset('assets/audio/explosion.mp3'),
    SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3'),
    SoLoud.instance.loadAsset('assets/audio/sample-1.ogg'),
    SoLoud.instance.loadAsset('assets/audio/tic-1.wav'),
    SoLoud.instance.loadAsset('assets/audio/tic-2.wav'),
  ];

  await Future.wait(sounds);
  // loading the same asset many times should not throw and just
  // display a warning.
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');

  deinit();
  return strBuf;
}

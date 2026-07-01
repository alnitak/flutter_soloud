import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

Future<OutputBuffer> testAutoDispose() async {
  final strBuf = OutputBuffer();
  await initialize();

  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/explosion.mp3',
    autoDispose: true,
  );

  sound.soundEvents.listen((event) {
    if (event.event == SoundEventType.soundDisposed) {
      strBuf.write('SoundEvent.soundDisposed');
    }
  });

  /// Play sample
  SoLoud.instance.play(sound);
  await delay(300);
  SoLoud.instance.play(sound);

  /// 3798ms explosion.mp3 sample duration
  await delay(5000);
  assert(
    strBuf.toString() == 'SoundEvent.soundDisposed',
    'Sound disposed event not triggered!',
  );
  strBuf.writeln();

  deinit();
  return strBuf;
}

import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

Future<OutputBuffer> testAutoDispose() async {
  final strBuf = OutputBuffer();
  await initialize();

  // --- Test 1: test audispose of a generic asset ---
  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/explosion.mp3',
    autoDispose: true,
  );

  sound.soundEvents.listen((event) {
    if (event.event == SoundEventType.soundDisposed) {
      strBuf.writeln('Test 1: SoundEvent.soundDisposed');
    }
  });

  /// Play sample
  SoLoud.instance.play(sound);
  await delay(300);
  SoLoud.instance.play(sound);

  /// 3798ms explosion.mp3 sample duration
  await delay(5000);
  assert(
    SoLoud.instance.isValidAudioSource(sound) == false,
    'Sound disposed event not triggered!',
  );

  // --- Test 2: test audispose of buffer stream ---
  final sound2Bytes = (await rootBundle.load('assets/audio/explosion.mp3'))
      .buffer
      .asUint8List();
  final sound2 = SoLoud.instance.setBufferStream(
    format: BufferType.auto,
    bufferingTimeNeeds: 0.3,
    maxBufferSizeBytes: 1024 * 1024 * 5,
    autoDispose: true,
  );
  SoLoud.instance.addAudioDataStream(sound2, sound2Bytes);
  SoLoud.instance.setDataIsEnded(sound2);

  sound2.soundEvents.listen((event) {
    if (event.event == SoundEventType.soundDisposed) {
      strBuf.writeln('Test 2: SoundEvent.soundDisposed');
    }
  });

  /// Play sample
  SoLoud.instance.play(sound2);
  await delay(300);
  SoLoud.instance.play(sound2);

  /// 3798ms explosion.mp3 sample duration
  await delay(5000);
  assert(
    SoLoud.instance.isValidAudioSource(sound2) == false,
    'Sound disposed event not triggered!',
  );

  strBuf.writeln();

  deinit();
  return strBuf;
}

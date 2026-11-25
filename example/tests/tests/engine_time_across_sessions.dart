import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Engine time keeps advancing across repeated init/play/deinit sessions.
///
/// Regression test for a multi-threaded engine freeze: after several
/// sessions `getEngineTime()` stopped advancing while a sound was playing.
/// Formerly the standalone `tests/repro.dart` loop.
Future<OutputBuffer> testEngineTimeAcrossSessions() async {
  final out = OutputBuffer();
  const sessions = 8;
  for (var session = 1; session <= sessions; session++) {
    await SoLoud.instance.init(bufferSize: 4096, channels: Channels.mono);
    SoLoud.instance.setGlobalVolume(0.2);
    final music = await SoLoud.instance.loadAsset(
      'assets/audio/8_bit_mentality.mp3',
    );
    SoLoud.instance.play(music);

    var last = SoLoud.instance.getEngineTime();
    var frozenCount = 0;
    for (var i = 0; i < 6; i++) {
      await delay(250);
      final t = SoLoud.instance.getEngineTime();
      if (t == last) frozenCount++;
      last = t;
    }
    SoLoud.instance.deinit();

    if (frozenCount > 2) {
      throw StateError(
        'engine time froze in session $session '
        '($frozenCount of 6 samples unchanged)',
      );
    }
    out.writeln('session $session ok, engineTime=${last.inMilliseconds}ms');
  }
  return out;
}

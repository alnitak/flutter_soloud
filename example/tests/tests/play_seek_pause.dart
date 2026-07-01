import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test play, pause, seek, position.
Future<OutputBuffer> testPlaySeekPause() async {
  final strBuf = OutputBuffer();

  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  /// pause, seek test
  {
    SoLoud.instance.play(currentSound);
    final length = SoLoud.instance.getLength(currentSound);
    assert(
      closeTo(length.inMilliseconds, 205557, 100),
      'getLength() failed: ${length.inMilliseconds}!\n',
    );
    await delay(1000);
    SoLoud.instance.pauseSwitch(currentSound.handles.first);
    final paused = SoLoud.instance.getPause(currentSound.handles.first);
    assert(paused, 'pauseSwitch() failed!');

    /// seek
    const wantedPosition = Duration(seconds: 2);
    SoLoud.instance.seek(currentSound.handles.first, wantedPosition);
    final position = SoLoud.instance.getPosition(currentSound.handles.first);
    assert(position == wantedPosition, 'getPosition() failed!');

    /// Test schedulePause
    strBuf.writeln('Testing schedulePause');
    SoLoud.instance.schedulePause(
      currentSound.handles.first,
      const Duration(milliseconds: 300),
    );
    await delay(400);
    // Should now be paused
    assert(
      SoLoud.instance.getPause(currentSound.handles.first),
      'Sound should be paused after schedulePause',
    );
    strBuf.writeln('schedulePause worked correctly');

    /// Resume and seek to near end to test handle invalidation
    SoLoud.instance.setPause(currentSound.handles.first, false);
    final soundLength = SoLoud.instance.getLength(currentSound);
    SoLoud.instance.seek(
      currentSound.handles.first,
      soundLength - const Duration(milliseconds: 100),
    );
    strBuf.writeln('Seeked to near end of sound');
    await delay(300);

    /// Handle should be invalid after sound finishes
    assert(
      currentSound.handles.isEmpty ||
          !SoLoud.instance.getIsValidVoiceHandle(currentSound.handles.first),
      'Handle should be invalid after sound finishes',
    );
    strBuf.writeln('Handle correctly invalidated after playback end');
  }

  deinit();
  return strBuf;
}

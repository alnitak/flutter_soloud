import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Regression tests for the errors the C++ side used to swallow.
///
/// Before this was fixed:
/// * `pauseSwitch`, `setPause` and `stop` returned `void`, so an invalid
///   handle (or a device that refused to restart) was completely invisible
///   to Dart;
/// * a playback method that could not create a voice still reported
///   `noError`, and Dart happily registered the bogus value as a handle;
/// * reaching the maximum number of active voices was reported as success,
///   so `play()` returned a handle that addressed nothing.
Future<OutputBuffer> testPlayerErrorPropagation() async {
  final strBuf = OutputBuffer();
  await initialize();

  final defaultVoiceCount = SoLoud.instance.getMaxActiveVoiceCount();

  final explosion = await SoLoud.instance.loadAsset(
    'assets/audio/explosion.mp3',
  );
  final song = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
  );

  // ---------------------------------------------------------------------
  // A successful play always returns a handle that addresses a live voice.
  // ---------------------------------------------------------------------
  final handle = SoLoud.instance.play(explosion);
  assert(
    SoLoud.instance.getIsValidVoiceHandle(handle),
    'play() returned a handle that is not a valid voice handle',
  );
  assert(
    explosion.handles.contains(handle),
    'play() did not register the new handle',
  );
  strBuf.writeln('play() returned the valid handle $handle');

  // ---------------------------------------------------------------------
  // Pause errors now reach Dart.
  // ---------------------------------------------------------------------
  SoLoud.instance.setPause(handle, true);
  assert(SoLoud.instance.getPause(handle), 'setPause(true) had no effect');
  SoLoud.instance.pauseSwitch(handle);
  assert(!SoLoud.instance.getPause(handle), 'pauseSwitch() had no effect');
  strBuf.writeln('setPause()/pauseSwitch() work on a valid handle');

  await SoLoud.instance.stop(handle);
  assert(
    !SoLoud.instance.getIsValidVoiceHandle(handle),
    'stop() did not stop the voice',
  );

  var thrown = false;
  try {
    SoLoud.instance.setPause(handle, true);
  } on SoLoudSoundHandleNotFoundCppException {
    thrown = true;
  }
  assert(thrown, 'setPause() on a stopped handle did not throw');

  thrown = false;
  try {
    SoLoud.instance.pauseSwitch(handle);
  } on SoLoudSoundHandleNotFoundCppException {
    thrown = true;
  }
  assert(thrown, 'pauseSwitch() on a stopped handle did not throw');
  strBuf.writeln('setPause()/pauseSwitch() throw on an invalid handle');

  // ---------------------------------------------------------------------
  // Stopping an already ended voice stays idempotent.
  // ---------------------------------------------------------------------
  await SoLoud.instance.stop(handle);
  strBuf.writeln('stop() on an already stopped handle completes normally');

  // ---------------------------------------------------------------------
  // Looping playback still works: the voice is created paused to set the
  // loop region up and is only unpaused once everything succeeded.
  // ---------------------------------------------------------------------
  final loopingHandle = SoLoud.instance.play(song, looping: true);
  assert(
    SoLoud.instance.getIsValidVoiceHandle(loopingHandle),
    'looping play() returned an invalid handle',
  );
  assert(
    !SoLoud.instance.getPause(loopingHandle),
    'looping play() left the voice paused',
  );
  assert(
    SoLoud.instance.getLooping(loopingHandle),
    'looping play() did not set the loop flag',
  );

  final pausedLoopingHandle = SoLoud.instance.play(
    song,
    looping: true,
    paused: true,
  );
  assert(
    SoLoud.instance.getPause(pausedLoopingHandle),
    'looping play(paused: true) started the voice',
  );
  await SoLoud.instance.stop(loopingHandle);
  await SoLoud.instance.stop(pausedLoopingHandle);
  strBuf.writeln('looping playback is unaffected');

  // ---------------------------------------------------------------------
  // Reaching the maximum active voice count stays non-blocking, but the
  // unusable handle must not be registered against the audio source.
  // ---------------------------------------------------------------------
  SoLoud.instance.setMaxActiveVoiceCount(4);
  // A source that has never been played, so the C++ side has no instance of
  // it to stop in order to make room for a new one.
  final neverPlayed = await SoLoud.instance.loadWaveform(
    WaveForm.square,
    false,
    1,
    1,
  );
  final songHandles = <SoundHandle>[];
  for (var i = 0; i < 4; i++) {
    songHandles.add(SoLoud.instance.play(song));
  }

  // Reaching the limit is non-blocking by design: it must not throw, but it
  // must not register the unusable handle either.
  final notPlayed = SoLoud.instance.play(neverPlayed);
  assert(
    !SoLoud.instance.getIsValidVoiceHandle(notPlayed),
    'play() beyond the max active voice count returned a usable handle',
  );
  assert(
    neverPlayed.handles.isEmpty,
    'play() registered a handle even though nothing was played',
  );
  strBuf.writeln(
    'play() past the max active voice count warns without throwing',
  );

  for (final h in songHandles) {
    if (SoLoud.instance.getIsValidVoiceHandle(h)) {
      await SoLoud.instance.stop(h);
    }
  }
  SoLoud.instance.setMaxActiveVoiceCount(defaultVoiceCount);

  // ---------------------------------------------------------------------
  // `Bus.playOnEngine()` also creates a voice and must return a real handle.
  // ---------------------------------------------------------------------
  final bus = SoLoud.instance.createMixingBus();
  final busHandle = bus.playOnEngine();
  assert(
    SoLoud.instance.getIsValidVoiceHandle(busHandle),
    'Bus.playOnEngine() returned an invalid handle',
  );
  assert(bus.isActive, 'Bus.playOnEngine() did not activate the bus');
  bus.dispose();
  strBuf.writeln('Bus.playOnEngine() returned the valid handle $busHandle');

  await SoLoud.instance.disposeSource(neverPlayed);
  await SoLoud.instance.disposeSource(explosion);
  await SoLoud.instance.disposeSource(song);
  deinit();

  strBuf.writeln('All player error propagation tests passed.');
  return strBuf;
}

import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test waveform.
Future<OutputBuffer> testCreateNotes() async {
  final strBuf = OutputBuffer();
  await initialize();

  final notes0 = await SoLoudTools.createNotes(
    octave: 0,
  );
  final notes1 = await SoLoudTools.createNotes(
    octave: 1,
  );
  final notes2 = await SoLoudTools.createNotes(
    octave: 2,
  );
  assert(
    notes0.length == 12 && notes1.length == 12 && notes2.length == 12,
    'SoLoudTools.createNotes() failed!\n',
  );

  SoLoud.instance.play(notes1[5]);
  SoLoud.instance.play(notes2[0]);
  await delay(350);
  await SoLoud.instance.stop(notes1[5].handles.first);
  await SoLoud.instance.stop(notes2[0].handles.first);

  SoLoud.instance.play(notes1[6]);
  SoLoud.instance.play(notes2[1]);
  await delay(350);
  await SoLoud.instance.stop(notes1[6].handles.first);
  await SoLoud.instance.stop(notes2[1].handles.first);

  SoLoud.instance.play(notes1[4]);
  SoLoud.instance.play(notes1[11]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes1[11].handles.first);

  SoLoud.instance.play(notes1[4]);
  SoLoud.instance.play(notes0[9]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes0[9].handles.first);

  SoLoud.instance.play(notes1[8]);
  SoLoud.instance.play(notes1[1]);
  await delay(1500);
  await SoLoud.instance.stop(notes1[8].handles.first);
  await SoLoud.instance.stop(notes1[1].handles.first);

  /// Test direct loadWaveform with different types
  strBuf.writeln('Testing direct loadWaveform calls');

  for (final waveform in [WaveForm.sin, WaveForm.square, WaveForm.saw]) {
    final waveSound = await SoLoud.instance.loadWaveform(
      waveform,
      false, // no super wave
      1,
      0,
    );

    // Set frequency before playing
    SoLoud.instance.setWaveformFreq(waveSound, 440);

    final waveHandle = SoLoud.instance.play(waveSound);
    strBuf.writeln('Played ${waveform.name} waveform at 440Hz');

    await delay(200);

    // Change frequency during playback
    SoLoud.instance.setWaveformFreq(waveSound, 880);
    strBuf.writeln('Changed frequency to 880Hz');

    await delay(200);

    await SoLoud.instance.stop(waveHandle);
    await SoLoud.instance.disposeSource(waveSound);
  }

  deinit();

  return strBuf;
}

// Waveform controls (type, frequency, scale, detune, super wave) on a playing
// synthetic waveform, with the real engine.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

Future<void> _pause(int ms) => Future<void>.delayed(Duration(milliseconds: ms));

void main() {
  setUpEngineTests();

  for (final waveform in WaveForm.values) {
    test('${waveform.name}: frequency and super wave controls', () async {
      final sound = await SoLoud.instance.loadWaveform(waveform, true, 1, 0.5);
      expect(sound.soundHash.isValid, isTrue);
      final handle = SoLoud.instance.play(sound, volume: 0.3);
      await _pause(30);

      for (var freq = 200.0; freq <= 800.0; freq += 200.0) {
        SoLoud.instance.setWaveformFreq(sound, freq);
        await _pause(20);
      }
      SoLoud.instance
        ..setWaveformSuperWave(sound, true)
        ..setWaveformScale(sound, 2)
        ..setWaveformDetune(sound, 0.8);
      await _pause(30);
      SoLoud.instance.setWaveformSuperWave(sound, false);
      await _pause(30);

      expect(
        SoLoud.instance.getIsValidVoiceHandle(handle),
        isTrue,
        reason: 'changing the parameters keeps the voice playing',
      );
      await SoLoud.instance.stop(handle);
      expect(SoLoud.instance.getIsValidVoiceHandle(handle), isFalse);
      await SoLoud.instance.disposeSource(sound);
      expect(SoLoud.instance.isValidAudioSource(sound), isFalse);
    });
  }

  test('setWaveform switches the type of a playing waveform', () async {
    final sound = await loadTestWaveform();
    final handle = SoLoud.instance.play(sound, volume: 0.3);
    for (final waveform in [
      WaveForm.square,
      WaveForm.saw,
      WaveForm.triangle,
      WaveForm.bounce,
    ]) {
      SoLoud.instance.setWaveform(sound, waveform);
      await _pause(30);
      expect(SoLoud.instance.getIsValidVoiceHandle(handle), isTrue);
    }
    await SoLoud.instance.disposeSource(sound);
    expect(
      SoLoud.instance.getIsValidVoiceHandle(handle),
      isFalse,
      reason: 'disposing the source stops its voices',
    );
  });
}

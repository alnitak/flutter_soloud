import 'dart:math';

import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/soloud.dart';

/// The `SoloudTools` class provides static methods to load audio files
/// from various sources, including assets, local files, and URLs.
///
class SoLoudTools {
  /// Returns a list of the 12 [AudioSource] notes of the given octave
  ///
  /// [octave] usually from 0 to 4
  static Future<List<AudioSource>> createNotes({
    int octave = 3,
    WaveForm waveForm = WaveForm.sin,
    bool superwave = true,
  }) async {
    assert(octave >= 0 && octave <= 4, '0 >= octave <= 4 is not true!');
    final startingFreq = 55.0 * (pow(2, (12 * octave) / 12));
    final notes = <AudioSource>[];
    for (var index = 0; index < 12; index++) {
      final sound = await SoLoud.instance.loadWaveform(
        waveForm,
        true,
        0.25,
        1,
      );
      final freq = startingFreq * (pow(2, index / 12));
      SoLoud.instance.setWaveformFreq(sound, freq);
      SoLoud.instance.setWaveformSuperWave(sound, superwave);
      notes.add(sound);
    }
    return notes;
  }
}

import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:flutter_soloud/src/utils/assets_manager.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:path/path.dart' as path;
import 'package:path_provider/path_provider.dart';

/// Old spelling of [SoLoudTools].
@Deprecated('Use SoLoudTools instead')
typedef SoloudTools = SoLoudTools;

/// The `SoloudTools` class provides static methods to load audio files
/// from various sources, including assets, local files, and URLs.
///
class SoLoudTools {
  static final Logger _log = Logger('flutter_soloud.SoloudTools');

  /// Loads an audio file from the assets folder.
  @Deprecated('Use SoLoud.loadAsset() instead')
  static Future<SoundProps?> loadFromAssets(
    String path, {
    LoadMode mode = LoadMode.memory,
  }) async {
    final f = await AssetsManager.getAssetFile(path);
    if (f == null) {
      _log.severe('Load from assets failed: Sound is null');
      return null;
    }

    return _finallyLoadFile(f, mode: mode);
  }

  /// Loads an audio file from the local file system.
  @Deprecated('Use SoLoud.loadFile() instead')
  static Future<SoundProps?> loadFromFile(
    String path, {
    LoadMode mode = LoadMode.memory,
  }) async {
    final file = File(path);
    if (file.existsSync()) {
      return _finallyLoadFile(file, mode: mode);
    } else {
      _log.severe('Load from file failed: File does not exist');
      return null;
    }
  }

  /// Fetches an audio file from a URL and loads it into the memory.
  @Deprecated('Use SoLoud.loadUrl() instead')
  static Future<SoundProps?> loadFromUrl(
    String url, {
    LoadMode mode = LoadMode.memory,
  }) async {
    try {
      final tempDir = await getTemporaryDirectory();
      final tempPath = tempDir.path;
      final filePath = path.join(tempPath, shortHash(url));
      final file = File(filePath);

      if (!file.existsSync()) {
        final response = await http.get(Uri.parse(url));
        if (response.statusCode == 200) {
          final byteData = response.bodyBytes;
          final buffer = byteData.buffer;
          await file.create(recursive: true);
          await file.writeAsBytes(
            buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
          );
        } else {
          _log.severe(() => 'Failed to fetch file from URL: $url');
          return null;
        }
      }
      return _finallyLoadFile(file, mode: mode);
    } catch (e, s) {
      _log.severe('Error while fetching file', e, s);
      return null;
    }
  }

  /// Let SoLoud try to load the file
  ///
  @Deprecated('Only used internally by deprecated methods')
  static Future<SoundProps?> _finallyLoadFile(
    File file, {
    LoadMode mode = LoadMode.memory,
  }) async {
    try {
      final result = await SoLoud.instance.loadFile(file.path, mode: mode);
      return result;
    } catch (e) {
      return null;
    }
  }

  /// Deprecated: Use [createNotes] instead.
  @Deprecated("Use 'createNotes' instead.")
  static Future<List<SoundProps>> initSounds({
    int octave = 3,
    WaveForm waveForm = WaveForm.sin,
    bool superwave = true,
  }) =>
      createNotes(
        octave: octave,
        waveForm: waveForm,
        superwave: superwave,
      );

  /// Returns a list of the 12 SoundProps (notes) of the given octave
  ///
  /// [octave] usually from 0 to 4
  static Future<List<SoundProps>> createNotes({
    int octave = 3,
    WaveForm waveForm = WaveForm.sin,
    bool superwave = true,
  }) async {
    assert(octave >= 0 && octave <= 4, '0 >= octave <= 4 is not true!');
    final startingFreq = 55.0 * (pow(2, (12 * octave) / 12));
    final notes = <SoundProps>[];
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

import 'dart:io';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/utils/assets_manager.dart';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';

/// The `SoloudLoadingTool` class provides static methods to load audio files
/// from various sources, including assets, local files, and URLs.
///
class SoloudTools {
  /// Loads an audio file from the assets folder.
  ///
  static Future<SoundProps?> loadFromAssets(String path) async {
    final f = await AssetsManager.getAssetFile(path);
    if (f == null) {
      debugPrint('Load from assets failed: Sound is null');
      return null;
    }

    return _finallyLoadFile(f);
  }

  /// Loads an audio file from the local file system.
  ///
  static Future<SoundProps?> loadFromFile(String path) async {
    final file = File(path);
    if (file.existsSync()) {
      return _finallyLoadFile(file);
    } else {
      debugPrint('Load from file failed: File does not exist');
      return null;
    }
  }

  /// Fetches an audio file from a URL and loads it into the memory.
  ///
  static Future<SoundProps?> loadFromUrl(String url) async {
    try {
      final now = DateTime.now();
      final tempDir = await getTemporaryDirectory();
      final tempPath = tempDir.path;
      final filePath = '$tempPath${Platform.pathSeparator}'
          '${now.year} ${now.month} ${now.day}';
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
          debugPrint('Failed to fetch file from URL: $url');
          return null;
        }
      }
      return _finallyLoadFile(file);
    } catch (e) {
      debugPrint('Error while fetching file: $e');
      return null;
    }
  }

  /// Let SoLoud try to load the file
  ///
  static Future<SoundProps?> _finallyLoadFile(File file) async {
    final result = await SoLoud().loadFile(file.path);
    if (!(result.error == PlayerErrors.noError ||
        result.error == PlayerErrors.fileAlreadyLoaded)) {
      return null;
    }
    return result.sound;
  }

  /// Returns a list of the 12 SoundProps (notes) of the given octave
  ///
  /// [octave] usually from 0 to 4
  static Future<List<SoundProps>> initSounds({
    int octave = 3,
    WaveForm waveForm = WaveForm.sin,
    bool superwave = true,
  }) async {
    assert(octave >= 0 && octave <= 4, '0 >= octave <= 4 is not true!');
    final startingFreq = 55.0 * (pow(2, (12 * octave) / 12));
    final notes = <SoundProps>[];
    for (var index = 0; index < 12; index++) {
      final ret = await SoLoud().loadWaveform(
        waveForm,
        true,
        0.25,
        1,
      );
      if (ret.error != PlayerErrors.noError) return [];
      final freq = startingFreq * (pow(2, index / 12));
      SoLoud().setWaveformFreq(ret.sound!, freq);
      SoLoud().setWaveformSuperWave(ret.sound!, superwave);
      notes.add(ret.sound!);
    }
    return notes;
  }
}

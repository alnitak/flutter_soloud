import 'dart:io';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/utils/assets_manager.dart';
import 'package:http/http.dart' as http;
import 'package:path_provider/path_provider.dart';

/// The `SoloudLoadingTool` class provides static methods to load audio files
/// from various sources, including assets, local files, and URLs.
class SoloudLoadingTool {
  /// Loads an audio file from the assets folder.
  static Future<SoundProps> loadFromAssets(String path) async {
    final f =
        await AssetsManager.getAssetFile(path);
    final loadRet = await SoLoud().loadFile(f.path);
    if (loadRet.error != PlayerErrors.noError) {
      throw Exception(loadRet.error);
    }
    if(loadRet.sound==null){
      throw Exception('Load from assets failed: Sound is null');
    }
    return loadRet.sound!;
  }
  /// Loads an audio file from the local file system.
  static Future<SoundProps> loadFromFile(String path)async{
    final file = File(path);
    if (!file.existsSync()) {
      final result = await SoLoud().loadFile(path);
      if(result.error != PlayerErrors.noError) {
        throw Exception(result.error);
      }
      if(result.sound==null){
        throw Exception('Load from assets failed: Sound is null');
      }
      return result.sound!;
    }else{
      throw Exception('Load from file failed: File does not exist');
    }
  }
  /// Fetches an audio file from a URL and loads it into the memory.
  static Future<SoundProps> loadFromUrl(String url)async{
    try {
      final response = await http.get(Uri.parse(url));
      if (response.statusCode == 200) {
        final tempDir = await getTemporaryDirectory();
        final tempPath = tempDir.path;
        final filePath = '$tempPath/${DateTime.now()}';
        final file = File(filePath);
        if (!file.existsSync()) {
          final byteData = response.bodyBytes;
          final buffer = byteData.buffer;
          await file.create(recursive: true);
          await file.writeAsBytes(
            buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
          );
        }
        final loadRet = await SoLoud().loadFile(file.path);
        if (loadRet.error != PlayerErrors.noError) {
          throw Exception(loadRet.error);
        }
        if(loadRet.sound==null){
          throw Exception('Load from assets failed: Sound is null');
        }
        return loadRet.sound!;
      } else {
        throw Exception('Failed to fetch file from URL: $url');
      }
    } catch (e) {
      throw Exception('Error while fetching file: $e');
    }
  }
}

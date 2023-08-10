import 'dart:io';
import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

/// Thanks to Maks Klimko

/// The [AssetsManager] class provides a static method to retrieve an asset
/// file and save it to the local file system.
///
class AssetsManager {
  /// Loads asset audio to temp file
  ///
  static Future<File?> getAssetFile(String assetsFile) async {
    final tempDir = await getTemporaryDirectory();
    final tempPath = tempDir.path;
    final filePath = '$tempPath/$assetsFile';
    final file = File(filePath);
    if (file.existsSync()) {
      return file;
    } else {
      try {
        final byteData = await rootBundle.load(assetsFile);
        final buffer = byteData.buffer;
        await file.create(recursive: true);
        await file.writeAsBytes(
          buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
        );
      } catch (e) {
        debugPrint('getAssetFile() error: $e');
        return null;
      }
      return file;
    }
  }
}

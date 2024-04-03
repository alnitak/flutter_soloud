import 'dart:io';

import 'package:flutter/services.dart';
import 'package:logging/logging.dart';
import 'package:path_provider/path_provider.dart';

/// Thanks to Maks Klimko

/// The [AssetsManager] class provides a static method to retrieve an asset
/// file and save it to the local file system.
///
@Deprecated('Use SoLoud.loadAsset instead')
class AssetsManager {
  static final Logger _log = Logger('flutter_soloud.AssetsManager');

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
      final ByteData byteData;
      try {
        byteData = await rootBundle.load(assetsFile);
      } catch (e, s) {
        // TODO(filiph): This probably shouldn't be a caught exception?
        //               Let the developer deal with it instead of silently
        //               failing.
        _log.severe("getAssetFile() couldn't load asset file", e, s);
        return null;
      }

      try {
        final buffer = byteData.buffer;
        await file.create(recursive: true);
        await file.writeAsBytes(
          buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
        );
      } catch (e, s) {
        // TODO(filiph): This probably shouldn't be a caught exception?
        //               Let the developer deal with it instead of silently
        //               failing.
        _log.severe("getAssetFile() couldn't write $file to disk", e, s);
        return null;
      }
      return file;
    }
  }
}

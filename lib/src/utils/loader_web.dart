import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';

/// A long-living helper class that loads assets and URLs into temporary files
/// that can be played by SoLoud.
@internal
class SoLoudLoader {
  SoLoudLoader();

  static final Logger _log = Logger('flutter_soloud.SoLoudLoader_web');

  /// To reflect [SoLoudLoader] for `io`.
  bool automaticCleanup = false;

  /// To reflect [SoLoudLoader] for `io`.
  Future<void> initialize() async {}

  /// Loads the asset with [key] (e.g. `assets/sound.mp3`), and return
  /// the file byte as `Uint8List` to be passed to `SoLoud.LoadMem` for
  /// the web platfom.
  ///
  /// Provide [assetBundle] if needed. By default, the method uses
  /// [rootBundle].
  ///
  /// Returns `null` if there's a problem with some implementation detail
  /// (e.g. cannot create temporary file).
  ///
  /// Throws [FlutterError] when the asset doesn't exist or cannot be loaded.
  /// (This is the same exception that [AssetBundle.load] would throw.)
  Future<AudioSource> loadAsset(
    String key,
    LoadMode mode, {
    AssetBundle? assetBundle,
  }) async {
    final ByteData byteData;
    final bundle = assetBundle ?? rootBundle;

    try {
      byteData = await bundle.load(key);
    } catch (e) {
      _log.severe("loadAsset() couldn't load $key from $bundle", e);
      // Fail-fast principle. If the developer tries to load an asset
      // that's not available, this should be seen during debugging,
      // and the developer should be able to catch and deal with the error
      // in a try-catch block.
      rethrow;
    }

    final buffer = byteData.buffer
        .asUint8List(byteData.offsetInBytes, byteData.lengthInBytes);
    final newAudioSource = SoLoud.instance.loadMem(key, buffer, mode: mode);
    return newAudioSource;
  }

  /// Optionally, you can provide your own [httpClient]. This is a good idea
  /// if you're loading several files in a short span of time (such as
  /// on program startup). When no [httpClient] is provided, this method
  /// will create a new one and close it afterwards.
  ///
  /// Throws [FormatException] if the [url] is invalid.
  /// Throws [SoLoudNetworkStatusCodeException] if the request fails.
  Future<AudioSource> loadUrl(
    String url,
    LoadMode mode, {
    http.Client? httpClient,
  }) async {
    final uri = Uri.parse(url);

    final Uint8List byteData;

    try {
      http.Response response;
      if (httpClient != null) {
        response = await httpClient.get(uri);
      } else {
        response = await http.get(uri);
      }
      if (response.statusCode == 200) {
        byteData = response.bodyBytes;
      } else {
        throw SoLoudNetworkStatusCodeException(
          response.statusCode,
          'Failed to fetch file from URL: $url',
        );
      }
    } catch (e) {
      _log.severe(() => 'Error fetching $url', e);
      rethrow;
    }

    final buffer = byteData.buffer
        .asUint8List(byteData.offsetInBytes, byteData.lengthInBytes);
    final newAudioSource = SoLoud.instance.loadMem(url, buffer);
    return newAudioSource;
  }
}

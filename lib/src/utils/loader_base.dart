// ignore_for_file: public_member_api_docs

import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:http/http.dart' as http;

/// Stub interface for unsupported plarforms.
interface class SoLoudLoader {
  /// To reflect [SoLoudLoader] for `io`.
  bool automaticCleanup = false;

  Future<void> initialize() async =>
      throw UnsupportedError('platform not supported');

  Future<AudioSource> loadAsset(
    String key,
    LoadMode mode, {
    AssetBundle? assetBundle,
  }) async =>
      throw UnsupportedError('platform not supported');

  Future<AudioSource> loadUrl(
    String url,
    LoadMode mode, {
    http.Client? httpClient,
  }) async =>
      throw UnsupportedError('platform not supported');
}

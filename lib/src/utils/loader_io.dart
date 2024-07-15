import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/src/audio_source.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/exceptions/exceptions.dart';
import 'package:flutter_soloud/src/soloud.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';
import 'package:meta/meta.dart';
import 'package:path/path.dart' as path;
import 'package:path_provider/path_provider.dart' as path_provider;

/// A long-living helper class that loads assets and URLs into temporary files
/// that can be played by SoLoud.
@internal
class SoLoudLoader {
  SoLoudLoader();

  static const String _temporaryDirectoryName = 'SoLoudLoader-Temp-Files';

  static final Logger _log = Logger('flutter_soloud.SoLoudLoader_io');

  /// When `true`, the loader will automatically call [cleanUp],
  /// deleting files in the temporary directory at various points.
  bool automaticCleanup = false;

  final Map<_TemporaryFileIdentifier, File> _temporaryFiles = {};

  Directory? _temporaryDirectory;

  /// Deletes temporary files. It is good practice to call this method
  /// once in a while, in order not to bloat the temporary directory.
  /// This is especially important when the application plays a lot of
  /// different files during its lifetime (e.g. a music player
  /// loading tracks from the network).
  ///
  /// For applications and games that play sounds from assets or from
  /// the file system, this is probably unnecessary, as the amount of data will
  /// be finite.
  Future<void> cleanUp() async {
    _log.finest('cleanUp() called');
    final directory = _temporaryDirectory;

    if (directory == null) {
      _log.warning("Temporary directory hasn't been initialized, "
          'yet cleanUp() is called.');
      return;
    }

    // Clear the set in case someone tries to access the files
    // while we're deleting them.
    _temporaryFiles.clear();

    try {
      final files = await directory.list(followLinks: false).toList();

      Future<bool> deleteFile(FileSystemEntity entity) async {
        try {
          await entity.delete(recursive: true);
        } on FileSystemException catch (e) {
          _log.warning(() => 'cleanUp() cannot remove ${entity.path}: $e');
          return false;
        }
        return true;
      }

      // Delete files in parallel.
      final results = await Future.wait(files.map(deleteFile));

      if (results.any((success) => !success)) {
        _log.severe('Cannot clean up temporary directory. See warnings above.');
      }

      await _temporaryDirectory?.delete(recursive: true);
    } on FileSystemException catch (e) {
      _log.severe('Cannot clean up temporary directory: $e');
    }
  }

  /// This method can be run safely several times.
  Future<void> initialize() async {
    _log.finest('initialize() called');
    if (_temporaryDirectory != null) {
      _log.fine(
        () => 'Loader has already been initialized. Not initializing again.',
      );
      if (automaticCleanup) {
        await cleanUp();
      }
      return;
    }

    final systemTempDir = await path_provider.getTemporaryDirectory();
    final directoryPath =
        path.join(systemTempDir.path, _temporaryDirectoryName);
    final directory = Directory(directoryPath);

    try {
      _temporaryDirectory = await directory.create();
    } catch (e) {
      _log.severe(
        "Couldn't initialize loader. Temporary directory couldn't be created.",
        e,
      );
      // There is no way we can recover from this. If we have nowhere to save
      // files, we can't play anything.
      rethrow;
    }

    _log.info(() => 'Temporary directory initialized at ${directory.path}');

    if (automaticCleanup) {
      await cleanUp();
    }
  }

  /// Loads the asset with [key] (e.g. `assets/sound.mp3`), and creates
  /// a temporary file that can be played by SoLoud.
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
    final id = _TemporaryFileIdentifier(_Source.asset, key);

    // TODO(filiph): Add the option to check the filesystem first.
    //               This could be a cache-invalidation problem (if the asset
    //               changes from one version to another). But it could speed
    //               up start up times.
    if (_temporaryFiles.containsKey(id)) {
      final existingFile = _temporaryFiles[id]!;
      if (existingFile.existsSync()) {
        _log.finest(() => 'Asset $key already exists as a temporary file.');
        final audioSource =
            SoLoud.instance.loadFile(existingFile.path, mode: mode);
        return audioSource;
      }
    }

    final directory = _temporaryDirectory;

    if (directory == null) {
      throw const SoLoudTemporaryFolderFailedException(
          "Temporary directory hasn't been initialized, "
          'yet loadAsset() is called.');
    }

    final newFilepath = _getFullTempFilePath(id);
    final newFile = File(newFilepath);

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

    final buffer = byteData.buffer;
    try {
      await newFile.create(recursive: true);
      await newFile.writeAsBytes(
        buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
      );
    } catch (e) {
      throw SoLoudTemporaryFolderFailedException(
        "loadAsset() couldn't write $newFile to disk",
      );
    }

    _temporaryFiles[id] = newFile;

    final audioSource = SoLoud.instance.loadFile(newFile.path, mode: mode);
    return audioSource;
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

    final id = _TemporaryFileIdentifier(_Source.url, url);

    // TODO(filiph): Add the option to check the filesystem first.
    //               This could be a cache-invalidation problem (if the asset
    //               changes from one version to another). But it could speed
    //               up start up times.
    if (_temporaryFiles.containsKey(id)) {
      final existingFile = _temporaryFiles[id]!;
      if (existingFile.existsSync()) {
        _log.finest(
          () => 'Sound from $url already exists as a temporary file.',
        );
        final newAudioSource = await SoLoud.instance.loadFile(
          existingFile.path,
          mode: mode,
        );
        return newAudioSource;
      }
    }

    final directory = _temporaryDirectory;

    if (directory == null) {
      throw const SoLoudTemporaryFolderFailedException(
          "Temporary directory hasn't been initialized, "
          'yet loadUrl() is called.');
    }

    final newFilepath = _getFullTempFilePath(id);
    final newFile = File(newFilepath);

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

    final buffer = byteData.buffer;
    try {
      await newFile.create(recursive: true);
      await newFile.writeAsBytes(
        buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
      );
    } catch (e) {
      throw SoLoudTemporaryFolderFailedException(
        "loadAsset() couldn't write $newFile to disk",
      );
    }

    _temporaryFiles[id] = newFile;

    final newAudioSource = await SoLoud.instance.loadFile(
      newFile.path,
      mode: mode,
    );

    return newAudioSource;
  }

  String _getFullTempFilePath(_TemporaryFileIdentifier id) {
    final directory = _temporaryDirectory;

    if (directory == null) {
      throw StateError("Temporary directory hasn't been initialized, "
          'yet _getTempFile() is called.');
    }

    return path.join(directory.absolute.path, id.asFilename);
  }
}

enum _Source {
  url,
  asset,
}

@immutable
class _TemporaryFileIdentifier {
  const _TemporaryFileIdentifier(this.source, this.path);

  final _Source source;

  final String path;

  String get asFilename =>
      'temp-sound-${source.name}-0x${path.hashCode.toRadixString(16)}';

  @override
  int get hashCode => Object.hash(source, path);

  @override
  bool operator ==(Object other) {
    return other is _TemporaryFileIdentifier &&
        other.source == source &&
        other.path == path;
  }
}

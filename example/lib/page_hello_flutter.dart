import 'dart:async';
import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Simple usecase of flutter_soloud plugin
class PageHelloFlutterSoLoud extends StatefulWidget {
  const PageHelloFlutterSoLoud({super.key});

  @override
  State<PageHelloFlutterSoLoud> createState() => _PageHelloFlutterSoLoudState();
}

class _PageHelloFlutterSoLoudState extends State<PageHelloFlutterSoLoud> {
  static final Logger _log = Logger('_PageHelloFlutterSoLoudState');

  AudioSource? currentSound;

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceAround,
          children: [
            /// pick audio file
            ElevatedButton(
              onPressed: () async {
                final paths = (await FilePicker.platform.pickFiles(
                  type: FileType.custom,
                  allowedExtensions: ['mp3', 'wav', 'ogg', 'flac'],
                  onFileLoading: print,
                  dialogTitle: 'Pick audio file\n(not for web)',
                ))
                    ?.files;
                if (paths != null) {
                  unawaited(playFile(paths.first.path!));
                }
              },
              child: const Text(
                'pick audio\n(not for web)',
                textAlign: TextAlign.center,
              ),
            ),

            /// pick audio file
            ElevatedButton(
              onPressed: () async {
                final paths = (await FilePicker.platform.pickFiles(
                  type: FileType.custom,
                  allowedExtensions: ['mp3', 'wav', 'ogg', 'flac'],
                  onFileLoading: print,
                  dialogTitle: 'Pick audio file',
                ))
                    ?.files;

                if (paths != null) {
                  if (kIsWeb) {
                    unawaited(playBuffer(paths.first.name, paths.first.bytes!));
                  } else {
                    final f = File(paths.first.path!);
                    final buffer = f.readAsBytesSync();
                    unawaited(playBuffer(paths.first.path!, buffer));
                  }
                }
              },
              child: const Text(
                'pick audio using "loadMem()"\n(all platforms)',
                textAlign: TextAlign.center,
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// play file
  Future<void> playFile(String file) async {
    /// stop any previous sound loaded
    if (currentSound != null) {
      try {
        await SoLoud.instance.disposeSource(currentSound!);
      } catch (e) {
        _log.severe('dispose error', e);
        return;
      }
    }

    /// load the audio file
    final AudioSource newSound;
    try {
      newSound = await SoLoud.instance.loadFile(file);
    } catch (e) {
      _log.severe('load error', e);
      return;
    }

    currentSound = newSound;

    /// play it
    await SoLoud.instance.play(currentSound!);
  }

  /// play bytes for web.
  Future<void> playBuffer(String fileName, Uint8List bytes) async {
    /// stop any previous sound loaded
    if (currentSound != null) {
      try {
        await SoLoud.instance.disposeSource(currentSound!);
      } catch (e) {
        _log.severe('dispose error', e);
        return;
      }
    }

    /// load the audio file
    final AudioSource newSound;
    try {
      newSound = await SoLoud.instance.loadMem(fileName, bytes);
    } catch (e) {
      _log.severe('load error', e);
      return;
    }

    currentSound = newSound;

    /// play it
    await SoLoud.instance.play(currentSound!);
  }
}

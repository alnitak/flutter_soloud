```dart
import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      home: HelloFlutterSoLoud(),
    );
  }
}

/// Simple usecase of flutter_soloud plugin
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  SoundProps? currentSound;

  @override
  void dispose() {
    SoLoud().stopIsolate();
    SoLoud().stopCapture();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
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
                  dialogTitle: 'Pick audio file',
                ))
                    ?.files;
                if (paths != null) {
                  unawaited(play(paths.first.path!));
                }
              },
              child: const Text('pick audio'),
            ),
            Column(
              children: [
                /// start the capture
                ElevatedButton(
                  onPressed: () async {
                    final a = SoLoud().initCapture();
                    final b = SoLoud().startCapture();
                    if (mounted &&
                        a == CaptureErrors.captureNoError &&
                        b == CaptureErrors.captureNoError) {
                      setState(() {});
                    }
                  },
                  child: const Text('start mic'),
                ),
                /// at this point, look at [hello_flutter.dar] to see
                /// how to read and manage frequencies or wave audio samples
              ],
            ),
          ],
        ),
      ),
    );
  }

  /// play file
  Future<void> play(String file) async {
    /// Start audio engine if not already
    if (!SoLoud().isIsolateRunning()) {
      await SoLoud().startIsolate().then((value) {
        if (value == PlayerErrors.noError) {
          debugPrint('isolate started');
        } else {
          debugPrint('isolate starting error: $value');
          return;
        }
      });
    }

    /// stop any previous sound loaded
    if (currentSound != null) {
      if (await SoLoud().disposeSound(currentSound!) !=
          PlayerErrors.noError) return;
    }

    /// load the audio file
    final newSound = await SoloudTools.loadFromFile(file);
    if (newSound == null) return;
    currentSound = newSound;

    /// play it
    final playRet = await SoLoud().play(currentSound!);
    if (playRet.error != PlayerErrors.noError) return;
    currentSound = playRet.sound;
  }
}
```
import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/audio_isolate.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud_example/controls.dart';
import 'package:flutter_soloud_example/page1.dart';
import 'package:flutter_soloud_example/page2.dart';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(),
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatelessWidget {
  const MyHomePage({super.key});

  @override
  Widget build(BuildContext context) {
    return const DefaultTabController(
      length: 3,
      child: Scaffold(
        body: Column(
          children: [
            Controls(),
            SizedBox(
              height: 40,
              child: TabBar(
                isScrollable: true,
                tabs: [
                  Tab(text: 'hello world!'),
                  Tab(text: 'visualizer'),
                  Tab(text: 'multi track'),
                ],
              ),
            ),
            Expanded(
              child: TabBarView(
                physics: NeverScrollableScrollPhysics(),
                children: [
                  HelloFlutterSoLoud(),
                  Page1(),
                  Page2(),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Simple usecase of flutter_soloud plugin
// ignore: must_be_immutable
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  SoundProps? currentSound;

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    debugPrint('${currentSound?.soundHash}');
    if (currentSound != null) {
      AudioIsolate().stopSound(currentSound!);
    }
    AudioIsolate().stopIsolate();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child:

            /// pick audio file
            ElevatedButton(
          onPressed: () async {
            final paths = (await FilePicker.platform.pickFiles(
              type: FileType.audio,
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
      ),
    );
  }

  /// play file
  Future<void> play(String file) async {
    /// Start audio engine if not already
    if (!AudioIsolate().isIsolateRunning()) {
      await AudioIsolate().startIsolate().then((value) {
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
      if ((await AudioIsolate().stopSound(currentSound!)) !=
          PlayerErrors.noError) return;
    }

    /// load the audio file
    final loadRet = await AudioIsolate().loadFile(file);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = loadRet.sound;

    /// play it
    final playRet = await AudioIsolate().play(currentSound!);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = playRet.sound;
  }
}

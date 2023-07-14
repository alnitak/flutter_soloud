import 'dart:async';
import 'dart:io';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:path_provider/path_provider.dart';
import 'package:flutter_soloud/audio_isolate.dart';

void main() async {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        dragDevices: {
          PointerDeviceKind.mouse,
          PointerDeviceKind.touch,
          PointerDeviceKind.stylus,
          PointerDeviceKind.unknown,
        },
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.only(top: 50, right: 8, left: 8),
        child: Column(
          // mainAxisAlignment: MainAxisAlignment.start,

          children: [
            Column(
              mainAxisAlignment: MainAxisAlignment.center,
              crossAxisAlignment: CrossAxisAlignment.center,
              children: [
                /// isolate start/stop   engine start/dispose
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .startIsolate()
                            .then((value) => debugPrint('isolate started'));
                      },
                      child: const Text('start isolate'),
                    ),
                    const SizedBox(width: 16),
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .stopIsolate()
                            .then((value) => debugPrint('isolate stopped'));
                      },
                      child: const Text('stop isolate'),
                    ),
                    const SizedBox(width: 32),
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .initEngine()
                            .then((value) => debugPrint('initEngine: $value'));
                      },
                      child: const Text('init'),
                    ),
                    const SizedBox(width: 16),
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .disposeEngine()
                            .then((value) => debugPrint('engine disposed'));
                      },
                      child: const Text('dispose'),
                    ),
                  ],
                ),
                const SizedBox(height: 32),

                /// loop start / stop
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .startLoop()
                            .then((value) => debugPrint('loop started'));
                      },
                      child: const Text('start loop'),
                    ),
                    const SizedBox(width: 16),
                    ElevatedButton(
                      onPressed: () {
                        AudioIsolate()
                            .stoptLoop()
                            .then((value) => debugPrint('loop stopped'));
                      },
                      child: const Text('stop loop'),
                    ),
                  ],
                ),
                const SizedBox(height: 32),
                MyButton(
                  assetsAudio: 'assets/audio/8_bit_mentality.mp3',
                  text: '8 bit mentality',
                ),
                const SizedBox(height: 32),
                MyButton(
                  assetsAudio: 'assets/audio/Tropical Beeper.mp3',
                  text: 'Tropical Beeper',
                ),
              ],
            ),
            const SizedBox(height: 12),
          ],
        ),
      ),
    );
  }
}

class MyButton extends StatelessWidget {
  MyButton({
    required this.assetsAudio,
    required this.text,
    super.key,
  });

  final String assetsAudio;
  final String text;

  final isPaused = ValueNotifier<bool>(true);
  final soundLength = ValueNotifier<double>(0);
  final soundPosition = ValueNotifier<double>(0);
  Timer? timer;
  int? audioHandler;

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        ElevatedButton(
          onPressed: () {
            if (audioHandler != null) return;
            playAsset(assetsAudio);
          },
          child: Text(text),
        ),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            ValueListenableBuilder<bool>(
              valueListenable: isPaused,
              builder: (_, paused, __) {
                return IconButton(
                  onPressed: () async {
                    if (audioHandler == null) return;
                    await AudioIsolate().pauseSwitch(audioHandler!);
                    await AudioIsolate().getPause(audioHandler!).then((value) {
                      if (value) {
                        stopTimer();
                      } else {
                        startTimer();
                      }
                      isPaused.value = value;
                    });
                  },
                  icon: paused
                      ? const Icon(Icons.pause_circle_outline, size: 48)
                      : const Icon(Icons.play_circle_outline, size: 48),
                  iconSize: 48,
                );
              },
            ),
            const SizedBox(width: 16),
            IconButton(
              onPressed: () {
                if (audioHandler == null) return;
                AudioIsolate().stop(audioHandler!).then((value) {
                  stopTimer();
                  audioHandler = null;
                });
              },
              icon: const Icon(Icons.stop_circle_outlined, size: 48),
              iconSize: 48,
            ),
          ],
        ),
        const SizedBox(height: 8),

        /// Seek slider
        ValueListenableBuilder<double>(
          valueListenable: soundLength,
          builder: (_, length, __) {
            return ValueListenableBuilder<double>(
              valueListenable: soundPosition,
              builder: (_, position, __) {
                if (position >= length) {
                  position = 0;
                  if (length == 0) length = 1;
                }

                return Row(
                  children: [
                    Text(position.toInt().toString()),
                    Expanded(
                      child: Slider.adaptive(
                        value: position,
                        max: length < position ? position : length,
                        onChanged: (value) async {
                          stopTimer();
                          soundPosition.value = value;
                          await AudioIsolate()
                              .seek(audioHandler!, value)
                              .then((value) {
                            debugPrint('seek $value  handler:$audioHandler');
                          });
                          startTimer();
                        },
                      ),
                    ),
                    Text(length.toInt().toString()),
                  ],
                );
              },
            );
          },
        ),
      ],
    );
  }

  /// play file
  void play(String file) {}

  /// plays an assets file
  Future<void> playAsset(String assetsFile) async {
    final audioFile = await getAssetFile(assetsFile);
    await AudioIsolate().playFile(audioFile.path).then((value) {
      debugPrint('playFile: $value');
      audioHandler = value.handle;
      startTimer();
    });
    if (audioHandler != null) {
      await AudioIsolate().getLength(audioHandler!).then((value) {
        debugPrint('getLength: $value');
        soundLength.value = value;
      });
    }
  }

  /// get the assets file and copy it to the temp dir
  Future<File> getAssetFile(String assetsFile) async {
    final tempDir = await getTemporaryDirectory();
    final tempPath = tempDir.path;
    final filePath = '$tempPath/$assetsFile';
    final file = File(filePath);
    if (file.existsSync()) {
      return file;
    } else {
      final byteData = await rootBundle.load(assetsFile);
      final buffer = byteData.buffer;
      await file.create(recursive: true);
      return file.writeAsBytes(
        buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
      );
    }
  }

  /// start timer to update the audio position slider
  void startTimer() {
    timer = Timer.periodic(const Duration(seconds: 1), (_) {
      AudioIsolate().getPosition(audioHandler!).then((value) {
        soundPosition.value = value;
      });
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

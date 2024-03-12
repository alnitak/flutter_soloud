import 'dart:async';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example to test `mode: LoadMode.disk` parameter to seek all audio files
/// with a single slider.
///
/// When using `SoloudTools.loadFrom*()` you can choose to load the audio
/// as a stream. This means that the audio is decoded when needed when
/// using `mode: LoadMode.disk` parameter. By default `LoadMode.disk` is used,
/// hence the audio file is loaded as raw data into memory.
///
/// `mode: LoadMode.memory` this will be useful when
/// loading few audio files, ie for game sounds, mainly used to prevent
/// gaps or lags when starting/seeking a sound (less CPU, more memory allocated).
/// `mode: LoadMode.disk` the audio data is loaded from the given
/// file when needed (more CPU, less memory allocated).
/// The drawback is seeking: when using MP3s the seek operation is
/// performed but there will be delays. This occurs because
/// the MP3 codec must compute each frame length to gain a new position.
/// This mode is useful ie for background music, not for a music player
/// where a seek slider for MP3s is a must.
/// If you need seeking MP3s, please, use `mode: LoadMode.memory` instead,
/// or other audio formats!
///
/// Please, in the the following example, add as many
/// `await addSound('audio/path');` you wish in the `start()` method.

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(useMaterial3: true),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        // enable mouse dragging
        dragDevices: PointerDeviceKind.values.toSet(),
      ),
      home: const MyHomePage(),
    );
  }
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key});

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  List<SoundProps> sounds = [];
  bool isStarted = false;
  double minLength = double.maxFinite;
  ValueNotifier<double> seekPos = ValueNotifier(0);
  late Timer timer;

  @override
  void initState() {
    super.initState();
  }

  Future<void> start() async {
    await SoLoud.instance.initialize().then((value) {
      if (value == PlayerErrors.noError) {
        debugPrint('isolate started');
        if (context.mounted) {
          isStarted = true;
        }
      } else {
        debugPrint('isolate starting error: $value');
      }
    });

    await SoLoud.instance.disposeAllSound();
    sounds.clear();
    seekPos.value = 0;

    /// Add here the complete audio file path.
    /// MP3s will have lags problem when seeking
    await addSound('');

    for (final s in sounds) {
      await SoLoud.instance.play(s);
    }
    if (context.mounted) setState(() {});

    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (sounds.isEmpty || sounds[0].handles.isEmpty) return;
      final p = SoLoud.instance.getPosition(sounds[0].handles.first).position;
      if (p <= minLength) seekPos.value = p;
    });
  }

  Future<void> addSound(String file) async {
    var l = 0.0;
    final ret = await SoLoud.instance.loadFile(file, mode: LoadMode.disk);
    assert(ret.error == PlayerErrors.noError, 'Error: ${ret.error}');
    final s = ret.sound!;
    sounds.add(s);
    l = SoLoud.instance.getLength(s).length;
    if (l < minLength) minLength = l;
  }

  Future<void> stop() async {
    await SoLoud.instance.shutdown();
    SoLoudCapture.instance.stopCapture();
    sounds.clear();
  }

  @override
  void dispose() {
    stop();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Scaffold(
        body: Column(
          children: [
            ElevatedButton(
              onPressed: start,
              child: const Text('start'),
            ),
            ...List.generate(
              sounds.length,
              (index) {
                return SoundRow(soundProps: sounds[index]);
              },
            ),
            ValueListenableBuilder(
              valueListenable: seekPos,
              builder: (_, v, __) {
                return Slider(
                  max: minLength,
                  value: v,
                  onChanged: (value) {
                    seekPos.value = value;
                    for (final s in sounds) {
                      if (s.handles.isEmpty) continue;
                      SoLoud.instance.seek(s.handles.first, value);
                    }
                  },
                );
              },
            ),
          ],
        ),
      ),
    );
  }
}

class SoundRow extends StatefulWidget {
  const SoundRow({
    required this.soundProps,
    super.key,
  });
  final SoundProps soundProps;

  @override
  State<SoundRow> createState() => _SoundRowState();
}

class _SoundRowState extends State<SoundRow> {
  late final Timer timer;
  double pos = 0;
  double max = 999999;

  @override
  void initState() {
    super.initState();
    max = SoLoud.instance.getLength(widget.soundProps).length;
    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (widget.soundProps.handles.isEmpty) return;
      pos =
          SoLoud.instance.getPosition(widget.soundProps.handles.first).position;
      if (context.mounted) setState(() {});
    });
  }

  @override
  void dispose() {
    timer.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Text(widget.soundProps.soundHash.toString()),
        Slider.adaptive(max: max, value: pos, onChanged: null),
        Text(pos.toStringAsFixed(2)),
      ],
    );
  }
}

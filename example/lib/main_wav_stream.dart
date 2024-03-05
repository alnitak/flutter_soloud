import 'dart:async';
import 'dart:ui';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Example using Soloud::WavStream.
/// When using `SoloudTools.loadFrom*()` you can choose to load the audio
/// as a stream. This means that the audio is decoded when needed when
/// using `loadIntoMem: false` parameter. By default is `true`, hence the audio
/// file is loaded as raw data into memory.
/// 
/// `loadIntoMem: true` this will be useful when the audio is short or when
/// loading few audio files, ie for game sounds, mainly used to prevent
/// gaps or lags when starting a sound (less CPU, more memory allocated).
/// `loadIntoMem: false` the audio data is loaded from the given
/// file when needed (more CPU, less memory allocated).
/// The drawback is seeking: when using mp3s the seek operation is not 
/// performed due to lags. This occurs because the mp3 codec must compute 
/// each frame length to gain a new position.
/// This mode is useful ie for background music, not for a music player
/// where a seek slider for mp3s is a must.
/// If you need seeking mp3, please, use `loadIntoMem`=true instead, 
/// or other audio formats!
/// 
/// Please, in the the following example, add as many `await addSound('');`
/// you wish in the `start()` method.



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
    await SoLoud().startIsolate().then((value) {
      if (value == PlayerErrors.noError) {
        debugPrint('isolate started');
        if (context.mounted) {
          isStarted = true;
        }
      } else {
        debugPrint('isolate starting error: $value');
      }
    });

    await SoLoud().disposeAllSound();
    sounds.clear();
    seekPos.value = 0;

    /// Add here the complete audio file path
    await addSound('/complete/audio/file/name');

    for (final s in sounds) {
      await SoLoud().play(s);
    }
    if (context.mounted) setState(() {});

    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (sounds.isEmpty || sounds[0].handle.isEmpty) return;
      final p = SoLoud().getPosition(sounds[0].handle.first).position;
      if (p <= minLength) seekPos.value = p;
    });
  }

  Future<void> addSound(String file) async {
    var l = 0.0;
    final s = await SoloudTools.loadFromFile(file, loadIntoMem: false);
    sounds.add(s!);
    l = SoLoud().getLength(s).length;
    if (l < minLength) minLength = l;
  }

  Future<void> stop() async {
    await SoLoud().disposeAllSound();
    await SoLoud().stopIsolate();
    SoLoud().stopCapture();
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
                      if (s.handle.isEmpty) continue;
                      SoLoud().seek(s.handle.first, value);
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
    max = SoLoud().getLength(widget.soundProps).length;
    timer = Timer.periodic(const Duration(milliseconds: 100), (timer) {
      if (widget.soundProps.handle.isEmpty) return;
      pos = SoLoud().getPosition(widget.soundProps.handle.first).position;
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

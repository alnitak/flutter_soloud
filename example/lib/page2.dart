import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/audio_isolate.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:path_provider/path_provider.dart';

class Page2 extends StatefulWidget {
  const Page2({super.key});

  @override
  State<Page2> createState() => _Page2State();
}

class _Page2State extends State<Page2> {
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
              children: [
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

class MyButton extends StatefulWidget {
  MyButton({
    required this.assetsAudio,
    required this.text,
    super.key,
  });

  final String assetsAudio;
  final String text;

  @override
  State<MyButton> createState() => _MyButtonState();
}

class _MyButtonState extends State<MyButton> {
  final isPaused = ValueNotifier<bool>(true);

  final soundLength = ValueNotifier<double>(0);

  final soundPosition = ValueNotifier<double>(0);

  Timer? timer;

  StreamSubscription<StreamSoundEvent>? _subscription;
  SoundProps? sound;

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    _subscription?.cancel();
    AudioIsolate().stopIsolate();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        ElevatedButton(
          onPressed: () async {
            if (sound != null) return;
            await playAsset(widget.assetsAudio);
          },
          child: Text(widget.text),
        ),
        Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            ValueListenableBuilder<bool>(
              valueListenable: isPaused,
              builder: (_, paused, __) {
                return IconButton(
                  onPressed: () async {
                    if (sound == null) return;
                    await AudioIsolate().pauseSwitch(sound!.handle);
                    await AudioIsolate().getPause(sound!.handle).then((value) {
                      if (value.pause) {
                        stopTimer();
                      } else {
                        startTimer();
                      }
                      isPaused.value = value.pause;
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
                if (sound == null) return;
                AudioIsolate().stop(sound!.handle);
                stopTimer();
                sound = null;
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
                          if (sound == null) return;
                          stopTimer();
                          soundPosition.value = value;
                          await AudioIsolate()
                              .seek(sound!.handle, value)
                              .then((value) {
                            debugPrint('seek $value  handler:${sound!.handle}');
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

  /// plays an assets file
  Future<void> playAsset(String assetsFile) async {
    final audioFile = await getAssetFile(assetsFile);
    await AudioIsolate().playFile(audioFile.path).then((value) {
      if (value.error != PlayerErrors.noError) return;

      sound = value.sound;

      /// Listen to this sound events
      _subscription = sound!.soundEvents.stream.listen(
        (event) {
          print(
              '@@@@@@@@@@@ StreamSoundEvent for handle: ${event.sound.handle}');
          stopTimer();
          _subscription?.cancel();
          sound!.soundEvents.close(); // TODO: put this elsewhere
          sound = null;
        },
      );

      startTimer();
      unawaited(AudioIsolate().getLength(sound!.handle).then((value) {
        soundLength.value = value.length;
      }));
    });
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
      AudioIsolate().getPosition(sound!.handle).then((value) {
        soundPosition.value = value.position;
      });
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

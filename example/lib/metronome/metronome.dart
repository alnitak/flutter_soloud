import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Metronome example.
///
/// For this example the player is initialized with a buffer size of 2048.
/// This means that for a playback at 44100 Hz the buffer is processed in
/// about 40ms. When we use the `play()` method, the sound will start at the
/// upcoming audio buffer and this could happen with a delay at least of
/// 40ms. If we reduce the buffer size also this gap will be shorter. A value
/// of 256 or 512 will reduce this latency but at the same time, the smaller
/// the buffer, the more likely the
/// system hits buffer underruns (ie, the play head marches on but there's no
/// data ready to be played) and the sound breaks down horribly.
///
/// By enabling the `use playClocked` checkbox, the ticks are scheduled with
/// `playClocked()` instead of `play()`: the engine uses the given "physics
/// time" to delay the start of the sounds with sample accuracy, so the
/// ticks won't get clumped to the start of the next outgoing audio buffer
/// even with the default buffer size of 2048.

void main() async {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });
  WidgetsFlutterBinding.ensureInitialized();

  /// Initialize the player.
  // ignore: avoid_redundant_argument_values
  await SoLoud.instance.init(bufferSize: 4096, channels: Channels.stereo);

  runApp(
    const MaterialApp(
      home: Metronome(),
    ),
  );
}

class Metronome extends StatefulWidget {
  const Metronome({super.key});

  @override
  State<Metronome> createState() => _MetronomeState();
}

class _MetronomeState extends State<Metronome> {
  /// delay between ticks.
  final delay = ValueNotifier<int>(100);

  /// duration of the tick sound.
  final tickDurationMs = ValueNotifier<int>(45);

  /// whether to use `playClocked` instead of `play` for the ticks.
  final useClocked = ValueNotifier<bool>(false);

  Timer? timer;
  AudioSource? tick1;
  AudioSource? tick2;

  int count = 0;

  /// The accumulated ideal time of the ticks used as the "physics time"
  /// for `playClocked`.
  Duration physicsTime = Duration.zero;

  @override
  void initState() {
    super.initState();
    SoLoud.instance.loadAsset('assets/audio/tic-1.wav').then((value) async {
      /// start playing the tick in a paused state, so it can be
      /// unpaused/paused in the `Timer` callback.
      tick1 = value;
      await SoLoud.instance
          .loadAsset('assets/audio/tic-2.wav')
          .then((value) async {
        /// start playing the tick in a paused state, so it can be
        /// unpaused/paused in the `Timer` callback.
        tick2 = value;
      });
    });
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Stack(
        children: [
          Padding(
            padding: const EdgeInsets.all(16),
            child: Center(
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text(
                    'Metronome example',
                    textScaler: TextScaler.linear(3),
                  ),
                  const SizedBox(height: 32),
                  ValueListenableBuilder<int>(
                    valueListenable: delay,
                    builder: (_, ms, __) {
                      return Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Slider.adaptive(
                            min: 20,
                            max: 500,
                            value: ms.toDouble(),
                            onChanged: (value) {
                              physicsTime = Duration.zero;
                              SoLoud.instance.resetStreamTime();
                              delay.value = value.toInt();
                              start();
                            },
                          ),
                          Text('delay ms: $ms  BPM: ${60000 ~/ ms}'),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 16),
                  ValueListenableBuilder<bool>(
                    valueListenable: useClocked,
                    builder: (_, clocked, __) {
                      return CheckboxListTile(
                        title: const Text('use playClocked'),
                        subtitle: const Text(
                          'schedule the ticks with sample accuracy',
                        ),
                        value: clocked,
                        onChanged: (value) {
                          physicsTime = Duration.zero;
                          SoLoud.instance.resetStreamTime();
                          useClocked.value = value ?? false;
                        },
                      );
                    },
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  void start() {
    timer?.cancel();
    physicsTime = Duration.zero;
    SoLoud.instance.resetStreamTime();
    timer = Timer.periodic(Duration(milliseconds: delay.value), (_) {
      final sound = count % 8 == 0 ? tick2 : tick1;
      if (sound != null) {
        if (useClocked.value) {
          /// Accumulate the ideal tick time and pass it to
          /// [SoLoud.playClocked]: the ticks will be spread with sample
          /// accuracy inside the audio buffer instead of being clumped
          /// to its start.
          physicsTime += Duration(milliseconds: delay.value);
          SoLoud.instance.playClocked(sound, physicsTime);
        } else {
          SoLoud.instance.play(sound);
        }
      }
      count++;
    });
  }
}

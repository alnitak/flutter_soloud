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
/// By enabling one of the `playClocked` / `playScheduled` checkboxes, the
/// ticks are scheduled with sample accuracy instead of `play()`: the engine
/// delays the start of the sounds so the ticks won't get clumped to the
/// start of the next outgoing audio buffer even with the default buffer
/// size of 2048.
///
/// `playClocked()` is fed with the accumulated ideal "physics time" of the
/// ticks: the first call anchors that time to the audio clock and the
/// following calls are placed relative to that anchor.
///
/// `playScheduled()` instead takes an absolute time on the engine's own
/// clock: the anchor is done explicitly by reading `getEngineTime()` once
/// when the metronome (re)starts and adding the accumulated ideal tick
/// times to it.

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

/// The method used to play the metronome ticks.
enum _PlayMode {
  /// Plain [SoLoud.play]: ticks start at the next output buffer boundary.
  play,

  /// [SoLoud.playClocked]: ticks spaced with sample accuracy against the
  /// accumulated "physics time".
  clocked,

  /// [SoLoud.playScheduled]: ticks pinned to absolute engine times.
  scheduled,
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

  /// which method to use to play the ticks.
  final playMode = ValueNotifier<_PlayMode>(_PlayMode.play);

  Timer? timer;
  AudioSource? tick1;
  AudioSource? tick2;

  int count = 0;

  /// The accumulated ideal time of the ticks used as the "physics time"
  /// for `playClocked` and as the offset from [engineAnchor] for
  /// `playScheduled`.
  Duration physicsTime = Duration.zero;

  /// The engine-clock time (see [SoLoud.getEngineTime]) corresponding to
  /// [physicsTime] zero, used by `playScheduled`. A small lead is added so
  /// scheduled ticks always land comfortably in the future.
  Duration engineAnchor = Duration.zero;

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
                    'Metronome example\n(4096 buffer size, 2 channels)',
                    textScaler: TextScaler.linear(1.5),
                    textAlign: TextAlign.center,
                  ),
                  const Text(
                    'sample-accurate playback and scheduling of ticks with playClocked/playScheduled\n'
                    'perfect for metronomes, rhythm games, sequencers, etc.',
                    textAlign: TextAlign.center,
                  ),
                  const SizedBox(height: 32),
                  ValueListenableBuilder<int>(
                    valueListenable: delay,
                    builder: (_, ms, __) {
                      return Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          Slider.adaptive(
                            min: 30,
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
                  ValueListenableBuilder<_PlayMode>(
                    valueListenable: playMode,
                    builder: (_, mode, __) {
                      return Column(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          CheckboxListTile(
                            title: const Text('use play'),
                            subtitle: const Text(
                              'play the ticks as soon as possible '
                              '(clumps at buffer boundaries)',
                            ),
                            value: mode == _PlayMode.play,
                            onChanged: (_) => setMode(_PlayMode.play),
                          ),
                          CheckboxListTile(
                            title: const Text('use playClocked'),
                            subtitle: const Text(
                              'schedule the ticks with sample accuracy '
                              'against an accumulated physics time',
                            ),
                            value: mode == _PlayMode.clocked,
                            onChanged: (value) => setMode(
                              (value ?? false)
                                  ? _PlayMode.clocked
                                  : _PlayMode.play,
                            ),
                          ),
                          CheckboxListTile(
                            title: const Text('use playScheduled'),
                            subtitle: const Text(
                              'schedule the ticks start and stop with sample '
                              'accuracy at absolute engine times',
                            ),
                            value: mode == _PlayMode.scheduled,
                            onChanged: (value) => setMode(
                              (value ?? false)
                                  ? _PlayMode.scheduled
                                  : _PlayMode.play,
                            ),
                          ),
                        ],
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

  void setMode(_PlayMode mode) {
    physicsTime = Duration.zero;
    SoLoud.instance.resetStreamTime();
    if (mode == _PlayMode.scheduled) {
      anchorEngineClock();
    }
    playMode.value = mode;
  }

  /// Anchor [physicsTime] to the engine clock for `playScheduled`, with a
  /// small lead so scheduled ticks always land comfortably in the future
  /// (a voice can only be delayed, never advanced, and Dart timers often
  /// fire a few ms late).
  void anchorEngineClock() {
    engineAnchor =
        SoLoud.instance.getEngineTime() + const Duration(milliseconds: 200);
  }

  void start() {
    timer?.cancel();
    physicsTime = Duration.zero;
    SoLoud.instance.resetStreamTime();
    if (playMode.value == _PlayMode.scheduled) {
      anchorEngineClock();
    }
    timer = Timer.periodic(Duration(milliseconds: delay.value), (_) {
      final sound = count % 8 == 0 ? tick2 : tick1;
      if (sound != null) {
        switch (playMode.value) {
          case _PlayMode.clocked:

            /// Accumulate the ideal tick time and pass it to
            /// [SoLoud.playClocked]: the ticks will be spread with sample
            /// accuracy inside the audio buffer instead of being clumped
            /// to its start.
            physicsTime += Duration(milliseconds: delay.value);
            SoLoud.instance.playClocked(sound, physicsTime);
          case _PlayMode.scheduled:

            /// The same, but pinned to the engine clock: [engineAnchor]
            /// maps [physicsTime] zero to an absolute engine time.
            physicsTime += Duration(milliseconds: delay.value);
            SoLoud.instance.playScheduled(
              sound,
              engineAnchor + physicsTime,
              duration: const Duration(milliseconds: 20),
            );
          case _PlayMode.play:
            SoLoud.instance.play(sound);
        }
      }
      count++;
    });
  }
}

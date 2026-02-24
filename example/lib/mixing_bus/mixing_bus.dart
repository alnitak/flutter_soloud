import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

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

  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: HelloFlutterSoLoud(),
    ),
  );
}

class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  AudioSource? currentSound;

  @override
  void initState() {
    super.initState();
    SoLoud.instance
        .loadAsset('assets/audio/IveSeenThings.mp3')
        .then((sound) => currentSound = sound);
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          spacing: 8,
          children: [
            ElevatedButton(
              onPressed: () {
                SoLoud.instance
                    .createMixingBus(name: 'bus ${Buses().buses.length + 1}');
                setState(() {});
              },
              child: const Text('create bus'),
            ),
            Row(
              mainAxisSize: MainAxisSize.min,
              spacing: 8,
              children: [
                ElevatedButton(
                  onPressed: () async {
                    await SoLoud.instance.play(currentSound!, looping: true);
                  },
                  child: const Text('play sound on engine'),
                ),
                ElevatedButton(
                  onPressed: () {
                    Buses().buses.first.annexSound(currentSound!.handles.first);
                    setState(() {});
                  },
                  child: const Text('move 1st handle of sound on bus 1'),
                ),
              ],
            ),
            Wrap(
              children: [
                for (final bus in Buses().buses)
                  BusControls(
                    bus: bus,
                    onMustRefresh: () => setState(() {}),
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class BusControls extends StatelessWidget {
  const BusControls({required this.bus, this.onMustRefresh, super.key});

  final Bus bus;

  final VoidCallback? onMustRefresh;

  @override
  Widget build(BuildContext context) {
    /// The volume of the bus.
    final volumeNotifier = ValueNotifier<double>(1);

    /// The voice count of the bus.
    final voiceCountNotifier = ValueNotifier<int>(0);

    AudioSource? background;
    AudioSource? explosion;
    AudioSource? iVeSeenThings;

    SoLoud.instance
        .loadAsset('assets/audio/8_bit_mentality.mp3')
        .then((sound) => background = sound);

    SoLoud.instance
        .loadAsset('assets/audio/explosion.mp3')
        .then((sound) => explosion = sound);

    SoLoud.instance
        .loadAsset('assets/audio/IveSeenThings.mp3')
        .then((sound) => iVeSeenThings = sound);

    background?.soundEvents.listen((event) {
      dev.log('background sound event $event');
      voiceCountNotifier.value = bus.getActiveVoiceCount();
    });

    explosion?.soundEvents.listen((event) {
      dev.log('explosion sound event $event');
      voiceCountNotifier.value = bus.getActiveVoiceCount();
    });

    iVeSeenThings?.soundEvents.listen((event) {
      dev.log('ive seen things sound event $event');
      voiceCountNotifier.value = bus.getActiveVoiceCount();
    });

    return Container(
      margin: const EdgeInsets.all(8),
      padding: const EdgeInsets.all(20),
      decoration: BoxDecoration(
        borderRadius: BorderRadius.circular(22),
        border: Border.all(color: Colors.blue),
      ),
      child: Column(
        spacing: 8,
        children: [
          ValueListenableBuilder(
            valueListenable: voiceCountNotifier,
            builder: (context, value, child) {
              return Text(
                '${bus.name} - voices: $value',
                style:
                    const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
              );
            },
          ),
          Row(
            mainAxisSize: MainAxisSize.min,
            spacing: 8,
            children: [
              ElevatedButton(
                onPressed: () => bus.playOnEngine(volume: volumeNotifier.value),
                child: const Text('play bus'),
              ),
              ElevatedButton(
                onPressed: () {
                  bus.dispose();
                  onMustRefresh?.call();
                },
                child: const Text('dispose bus'),
              ),
            ],
          ),
          const SizedBox(height: 8),
          ElevatedButton(
            onPressed: () async {
              if (background == null) return;
              await bus.play(background!, volume: 0.2);
              voiceCountNotifier.value = bus.getActiveVoiceCount();
            },
            child: const Text('play background sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (explosion == null) return;
              await bus.play(explosion!);
              voiceCountNotifier.value = bus.getActiveVoiceCount();
            },
            child: const Text('play explosion sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              if (iVeSeenThings == null) return;
              await bus.play(iVeSeenThings!);
              voiceCountNotifier.value = bus.getActiveVoiceCount();
            },
            child: const Text('play ive seen things sound'),
          ),
          const SizedBox(height: 8),
          ValueListenableBuilder<double>(
            valueListenable: volumeNotifier,
            builder: (context, volume, child) {
              return Row(
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text('Volume: ${volume.toStringAsFixed(2)}'),
                  Slider(
                    value: volume,
                    max: 3,
                    onChanged: (value) {
                      volumeNotifier.value = value;
                      if (bus.soundHandle != null) {
                        SoLoud.instance.setVolume(bus.soundHandle!, value);
                      }
                    },
                  ),
                ],
              );
            },
          ),
          ElevatedButton(
            onPressed: () async {
              bus.filters.pitchShiftFilter.activate();
              bus.filters.pitchShiftFilter
                  .shift(soundHandle: bus.soundHandle)
                  .value = 2.5;
            },
            child: const Text('pitch shift filter'),
          ),
        ],
      ),
    );
  }
}

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
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          children: [
            ElevatedButton(
              onPressed: () {
                SoLoud.instance
                    .createBus(name: 'bus ${Buses().buses.length + 1}');
                setState(() {});
              },
              child: const Text('create bus'),
            ),
            ElevatedButton(
              onPressed: () async {
                final sound = await SoLoud.instance
                    .loadAsset('assets/audio/IveSeenThings.mp3');
                await SoLoud.instance.play(sound, looping: true);
              },
              child: const Text('play sound on engine'),
            ),
            Wrap(
              children: [
                for (final bus in Buses().buses)
                  BusControls(
                    bus: bus,
                    onDispose: () => setState(() {}),
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
  const BusControls({required this.bus, this.onDispose, super.key});

  final Bus bus;

  final VoidCallback? onDispose;

  @override
  Widget build(BuildContext context) {
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
          Text(
            bus.name,
            style: const TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
          ),
          ElevatedButton(
            onPressed: () {
              bus.dispose();
              onDispose?.call();
            },
            child: const Text('dispose bus'),
          ),
          ElevatedButton(
            onPressed: bus.playOnEngine,
            child: const Text('play bus'),
          ),
          ElevatedButton(
            onPressed: () async {
              final sound = await SoLoud.instance
                  .loadAsset('assets/audio/8_bit_mentality.mp3');
              sound.soundEvents.listen((event) {
                dev.log('background sound event $event');
              });
              bus.play(sound, volume: 0.2);
            },
            child: const Text('play background sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              final sound =
                  await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
              sound.soundEvents.listen((event) {
                dev.log('explosion sound event $event');
              });
              bus.play(sound);
            },
            child: const Text('play explosion sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              final sound = await SoLoud.instance
                  .loadAsset('assets/audio/IveSeenThings.mp3');
              sound.soundEvents.listen((event) {
                dev.log('ive seen things sound event $event');
              });
              bus.play(sound);
            },
            child: const Text('play ive seen things sound'),
          ),
          ElevatedButton(
            onPressed: () async {
              bus.filters.pitchShiftFilter.activate();
              bus.filters.pitchShiftFilter.shift.value = 1.5;
            },
            child: const Text('pitch shift filter'),
          ),
        ],
      ),
    );
  }
}

import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// playClocked APIs example.
///

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
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: ClockedExample(),
    ),
  );
}

class ClockedExample extends StatefulWidget {
  const ClockedExample({super.key});

  @override
  State<ClockedExample> createState() => _ClockedExampleState();
}

class _ClockedExampleState extends State<ClockedExample> {
  Timer? timer;
  List<AudioSource?> notes = [];
  AudioSource? tick1;

  /// The accumulated ideal time of the ticks used as the "physics time"
  /// for `playClocked`.
  Duration physicsTime = Duration.zero;

  @override
  void initState() {
    super.initState();
    SoLoud.instance.loadAsset('assets/audio/tic-1.wav').then((tick) async {
      tick1 = tick;
    });
    SoLoudTools.createNotes(
      superwave: false,
    ).then((notes) {
      this.notes = notes;
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
                    'Clocked Example',
                    textScaler: TextScaler.linear(3),
                  ),
                  const SizedBox(height: 32),
                  ElevatedButton(
                    onPressed: start,
                    child: const Text('Start'),
                  ),
                  ElevatedButton(
                    onPressed: start2,
                    child: const Text('Start2'),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  void start2() {
    const physicsTime = Duration(seconds: 1);
    SoLoud.instance.resetStreamTime();
    SoLoud.instance.playClocked(
      tick1!,
      physicsTime,
    );
    SoLoud.instance.playClocked(
      tick1!,
      physicsTime + const Duration(milliseconds: 1500),
    );
    // SoLoud.instance.scheduleStop(
    //   handle,
    //   physicsTime,
    // );
  }

  void start() {
    var physicsTime = Duration.zero;
    SoLoud.instance.resetStreamTime();
    for (final note in notes) {
      final handle = SoLoud.instance.playClocked(
        note!,
        physicsTime,
      );
      SoLoud.instance.scheduleStop(
        handle,
        physicsTime,
      );
      physicsTime += const Duration(milliseconds: 500);
    }
  }
}

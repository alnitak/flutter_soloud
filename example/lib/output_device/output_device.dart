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

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: HelloFlutterSoLoud(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class HelloFlutterSoLoud extends StatefulWidget {
  const HelloFlutterSoLoud({super.key});

  @override
  State<HelloFlutterSoLoud> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<HelloFlutterSoLoud> {
  late TextEditingController textEditingController;
  late final List<PlaybackDevice> devices;
  late PlaybackDevice currentDevice;
  AudioSource? currentSound;

  @override
  void initState() {
    super.initState();
    SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3').then((value) {
      currentSound = value;
      SoLoud.instance.play(currentSound!, looping: true);
    });
    
    devices = SoLoud.instance.listPlaybackDevices();
    assert(devices.isNotEmpty, 'No devices found!');

    currentDevice = devices.firstWhere(
      (d) => d.isDefault,
      orElse: () => devices.first,
    );
    textEditingController = TextEditingController(text: currentDevice.name);
  }

  @override
  void dispose() {
    SoLoud.instance.deinit();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: DropdownMenu(
          controller: textEditingController,
          onSelected: (value) async {
            /// When changing the output device, we need to reinitialize
            /// the player. All existing audio sources will be stopped and
            /// disposed as well.
            // currentDevice = devices[value!];
            // SoLoud.instance.deinit();
            // await SoLoud.instance.init(deviceId: currentDevice.id);

            // currentSound = await SoLoud.instance
            //     .loadAsset('assets/audio/8_bit_mentality.mp3');
            // await SoLoud.instance.play(currentSound!, looping: true);
            SoLoud.instance.changeDevice(newDevice: devices[value!]);
          },
          dropdownMenuEntries: [
            for (var i = 0; i < devices.length; i++)
              DropdownMenuEntry(
                value: i,
                label: '(${devices[i].id}) - ${devices[i].name}',
              ),
          ],
        ),
      ),
    );
  }
}

// ignore_for_file: avoid_print

import 'dart:developer' as dev;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/ui/bars.dart';
import 'package:logging/logging.dart';

void main() async {
  Logger.root.level = kDebugMode ? Level.FINEST : Level.INFO;
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

  // await SoLoud.instance.init();
  // SoLoudCapture.instance.initialize();
  // SoLoud.instance.setVisualizationEnabled(true);

  runApp(const MyApp());
}

class MyApp extends StatefulWidget {
  const MyApp({super.key});

  @override
  State<MyApp> createState() => _MyAppState();
}

class _MyAppState extends State<MyApp> {
  // final _flutterSoloudPlugin = FlutterSoloudWeb();
  final _flutterSoloudPlugin = SoLoud.instance;
  final _flutterSoloudCapturePlugin = SoLoudCapture.instance;
  AudioSource? audioSource;
  late SoundHash soundHash;
  late SoundHandle soundHandle;
  final smooth = ValueNotifier<double>(1);

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        appBar: AppBar(
          title: const Text('Plugin example app'),
        ),
        body: Center(
          child: Column(
            children: [
              OutlinedButton(
                onPressed: () async {
                  final e = _flutterSoloudCapturePlugin.listCaptureDevices();
                  final devices = StringBuffer();
                  for (final element in e) {
                    devices.write(element.isDefault ? 'X - ' : '  - ');
                    devices.write(element.name);
                    devices.write('\n');
                  }
                  devices.write(null);

                  print('CAPTURE list devices:\n$devices');
                },
                child: const Text('list devices'),
              ),
              OutlinedButton(
                onPressed: () async {
                  final e = _flutterSoloudCapturePlugin.startCapture();
                  print('CAPTURE start $e');
                },
                child: const Text('start capture'),
              ),
              const SizedBox(height: 20),
              ////////////////////////////////////////
              ////////////////////////////////////////
              ////////////////////////////////////////
              OutlinedButton(
                onPressed: () async {
                  try {
                    await SoLoud.instance.init();
                    // SoLoudCapture.instance.init();
                    // SoLoud.instance.setVisualizationEnabled(true);
                  } on Exception catch (_) {}
                },
                child: const Text('init'),
              ),
              OutlinedButton(
                onPressed: () async {
                  try {
                    SoLoud.instance.setVisualizationEnabled(true);
                  } on Exception catch (_) {}
                },
                child: const Text('VisualizationEnabled'),
              ),
              OutlinedButton(
                onPressed: () async {
                  // _flutterSoloudPlugin.sendMessage('voiceEndedCallback',7777);
                },
                child: const Text('send to worker'),
              ),
              OutlinedButton(
                onPressed: () async {
                  _flutterSoloudPlugin
                      .getFilterParamNames(FilterType.biquadResonantFilter);
                },
                child: const Text('getFilterParamNames'),
              ),
              OutlinedButton(
                onPressed: () async {
                  try {
                    audioSource = await _flutterSoloudPlugin.loadWaveform(
                      WaveForm.bounce,
                      true,
                      1,
                      1,
                    );
                    soundHash = audioSource!.soundHash;
                    print('main loadWaveform audioSource: $soundHash');
                  } on Exception catch (_) {}
                },
                child: const Text('load waveform'),
              ),
              OutlinedButton(
                onPressed: () async {
                  if (kIsWeb) {
                    final result = await FilePicker.platform
                        .pickFiles(type: FileType.any, allowMultiple: false);
                    if (result != null && result.files.isNotEmpty) {
                      final fileName = result.files.first.name;
                      final fileBytes = result.files.first.bytes;
                      try {
                        audioSource = await _flutterSoloudPlugin.loadMem(
                          fileName,
                          fileBytes!,
                        );
                        soundHash = audioSource!.soundHash;
                      } on Exception catch (_) {}
                    }
                  }
                },
                child: const Text('load mem picked file into'),
              ),
              OutlinedButton(
                onPressed: () async {
                  final byteData =
                      await rootBundle.load('assets/audio/explosion.mp3');
                  final buffer = byteData.buffer;
                  try {
                    audioSource = await _flutterSoloudPlugin.loadMem(
                      'assets/explosion.mp3',
                      buffer.asUint8List(),
                    );
                    soundHash = audioSource!.soundHash;
                  } on Exception catch (_) {}
                },
                child: const Text('loadMem explosion'),
              ),
              OutlinedButton(
                onPressed: () async {
                  try {
                    if (audioSource != null) {
                      soundHandle =
                          await _flutterSoloudPlugin.play(audioSource!);
                    }
                  } on Exception catch (_) {}
                },
                child: const Text('play'),
              ),
              ////////////////////////////////////////////////
              ////////////////////////////////////////////////
              ////////////////////////////////////////////////
              // OutlinedButton(
              //   onPressed: () {
              //     audioData.updateSamples();
              //     final s = StringBuffer();
              //     for (var i = 0; i < 20; i++) {
              //       s.write(
              //         audioData
              //             .get2D(SampleRow(0), SampleColumn(i))
              //             .toStringAsFixed(1),
              //       );
              //     }
              //     print('texture2d: $s');
              //   },
              //   child: const Text('get texture'),
              // ),
              ////////////////////////////////////////////////
              ////////////////////////////////////////////////
              ////////////////////////////////////////////////

              OutlinedButton(
                onPressed: _flutterSoloudPlugin.deinit,
                child: const Text('dispose'),
              ),
              const SizedBox(height: 40),
              ValueListenableBuilder<double>(
                valueListenable: smooth,
                builder: (_, value, __) {
                  return Slider(
                      value: value,
                      onChanged: (v) {
                        smooth.value = v;
                        try {
                          SoLoud.instance.setFftSmoothing(smooth.value);
                        } on Exception catch (_) {}
                      },);
                },
              ),
              const Bars(),
            ],
          ),
        ),
      ),
    );
  }
}

import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows the use of the parametric equalizer filter.
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
      home: ParametricEq(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class ParametricEq extends StatefulWidget {
  const ParametricEq({super.key});

  @override
  State<ParametricEq> createState() => _ParametricEqState();
}

class _ParametricEqState extends State<ParametricEq> {
  final bandsNumber = ValueNotifier<int>(3);
  static const maxBandsNumber = 64;
  static const minGain = 0.0;
  static const maxGain = 4.0;
  final gains = List<ValueNotifier<double>>.generate(
    maxBandsNumber,
    (index) => ValueNotifier<double>(0),
  );
  final soloud = SoLoud.instance;
  AudioSource? source;

  @override
  void initState() {
    super.initState();
    soloud.loadAsset('assets/audio/8_bit_mentality.mp3').then((s) {
      source = s;
      soloud.play(source!, looping: true);
      soloud.filters.parametricEqFilter.activate();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: SingleChildScrollView(
          child: Column(
            children: [
              /// Number of bands
              ValueListenableBuilder(
                valueListenable: bandsNumber,
                builder: (context, value, child) {
                  return Row(
                    children: [
                      const Text('Bands Number'),
                      Expanded(
                        child: Slider(
                          value: value.toDouble(),
                          min: 1,
                          max: maxBandsNumber.toDouble(),
                          divisions: maxBandsNumber,
                          label: value.toString(),
                          onChanged: (value) {
                            bandsNumber.value = value.toInt();
                            // soloud.filters.parametricEqFilter.
                          },
                        ),
                      ),
                      Text(value.toString()),
                    ],
                  );
                },
              ),

              /// Band sliders
              ValueListenableBuilder(
                valueListenable: bandsNumber,
                builder: (context, value, child) {
                  return Column(
                    children: List.generate(
                      value,
                      (index) => ValueListenableBuilder(
                        valueListenable: gains[index],
                        builder: (context, value, child) {
                          return Row(
                            children: [
                              Text('#$index'),
                              Expanded(
                                child: Slider(
                                  value: value,
                                  min: minGain,
                                  max: maxGain,
                                  label: index.toString(),
                                  onChanged: (value) {
                                    gains[index].value = value;
                                  },
                                ),
                              ),
                              Text(value.toStringAsFixed(2)),
                            ],
                          );
                        },
                      ),
                    ),
                  );
                },
              ),
            ],
          ),
        ),
      ),
    );
  }
}

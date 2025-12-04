import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows the use of the parametric equalizer filter.
///
/// The parametric equalizer filter is a frequency-domain filter that uses the
/// Fast Fourier Transform (FFT) to analyze the frequency spectrum of the
/// audio signal and apply a gain to each frequency band.
///
/// The filter has the following parameters:
/// - `numBands`: the number of frequency bands (default: 3. 1 is the minimum
///   value and 64 is the maximum value)
/// - `stftWindowSize`: the size of the FFT window (default: 1024, must be a
///   power of 2. 32 is the minimum value and 4096 is the maximum value)
/// - `bandGain`: the gain of each frequency band (default: 1. 0 means no gain,
///   values > 1. 0 increase the gain, values < 1. 0 decrease the gain)
///
/// The `stftWindowSize` parameter controls the size of the FFT window used to
/// analyze the frequency spectrum of the audio signal. A larger window size
/// will provide more accurate frequency analysis but will also increase the
/// latency of the filter.
///
/// The filter is activated by calling the `activate` method and deactivated by
/// calling the `deactivate` method.
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
  final windowSize = ValueNotifier<int>(1024);
  final bandsNumber = ValueNotifier<int>(3);
  static const maxBandsNumber = 64;
  static const minGain = 0.0;
  static const maxGain = 4.0;
  final gains = List<ValueNotifier<double>>.generate(
    maxBandsNumber,
    (index) => ValueNotifier<double>(1),
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

  /// Find nearest power of 2 greater than or equal to x
  int nextPowerOf2(int x) {
    if (x < 1) return 1;
    var y = x - 1;
    y |= y >> 1;
    y |= y >> 2;
    y |= y >> 4;
    y |= y >> 8;
    y |= y >> 16;
    return y + 1;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: SingleChildScrollView(
          child: Column(
            children: [
              /// Activate / deactivate filter
              Row(
                spacing: 32,
                children: [
                  OutlinedButton(
                    onPressed: () {
                      if (soloud.filters.parametricEqFilter.isActive) return;
                      soloud.filters.parametricEqFilter.activate();
                      soloud.filters.parametricEqFilter.numBands.value =
                          bandsNumber.value.toDouble();
                      for (var i = 0; i < bandsNumber.value; i++) {
                        soloud.filters.parametricEqFilter.bandGain(i).value =
                            gains[i].value;
                      }
                      soloud.filters.parametricEqFilter.stftWindowSize.value =
                          windowSize.value.toDouble();
                    },
                    child: const Text('Activate'),
                  ),
                  OutlinedButton(
                    onPressed: () {
                      if (!soloud.filters.parametricEqFilter.isActive) return;
                      soloud.filters.parametricEqFilter.deactivate();
                    },
                    child: const Text('Deactivate'),
                  ),
                ],
              ),

              /// Window size (must be power of 2)
              ValueListenableBuilder(
                valueListenable: windowSize,
                builder: (context, value, child) {
                  return Row(
                    children: [
                      const Text('Window Size'),
                      Expanded(
                        child: Slider(
                          value: value.toDouble(),
                          min: 32,
                          max: 4096,
                          label: value.toString(),
                          onChanged: (value) {
                            /// Find nearest power of 2
                            final p2 = nextPowerOf2(value.toInt());
                            windowSize.value = p2;
                            soloud.filters.parametricEqFilter.stftWindowSize
                                .value = p2.toDouble();
                          },
                        ),
                      ),
                      Text(value.toString()),
                    ],
                  );
                },
              ),

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
                            soloud.filters.parametricEqFilter.numBands.value =
                                value;
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
                                    soloud.filters.parametricEqFilter
                                        .bandGain(index)
                                        .value = value;
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

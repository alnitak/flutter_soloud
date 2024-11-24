import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows the use of the compressor filter.
///
/// Happens that playing multple sounds at the same time can create a lot of
/// noise and output distortion. The compressor filter can be used to reduce
/// this behaviour.
///
/// In this example we play a sound multiple times to have a distorted sound.
/// Then we activate the compressor filter and adjust the compressor parameters.
///
/// ** Compressor parameters**:
/// 'wet`: Mix between original (dry) and compressed (wet) signal. 0.0 = 100%
/// dry, 1.0 = 100% wet.
///
/// `threshold`: The threshold in dB at which compression starts. Values
/// lower than the threshold will be compressed.
///
/// `makeupGain`: The make-up gain in dB applied to the compressed signal
/// to compensate for loss in volume due to compression.
///
/// `kneeWidth`: The width in dB of the soft knee where compression smoothly
/// begins to take effect. A larger value smooths compression.
///
/// `ratio`: The compression ratio. The amount by which input exceeding the
/// threshold will be reduced. For example, 4:1 reduces 4 dB of input to 1 dB.
///
/// `attackTime`: The time in ms for the compressor to react to a sudden
/// increase in input level.
///
/// `releaseTime`: The time in ms for the compressor to release the gain
/// reduction after the input level falls below the threshold.
///
/// Tip: when using other filter, it's preferable to activate the compressor
/// filter after all the others. Doing this if underlaying filters gain the
/// volume too much, this filter will act on top of it.

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
  await SoLoud.instance.init(sampleRate: 22050);

  runApp(
    const MaterialApp(
      home: CompressorExample(),
    ),
  );
}

class CompressorExample extends StatefulWidget {
  const CompressorExample({super.key});

  @override
  State<CompressorExample> createState() => _CompressorExampleState();
}

class _CompressorExampleState extends State<CompressorExample> {
  final compressor = SoLoud.instance.filters.compressorFilter;
  AudioSource? sound;
  late double wet;
  late double threshold;
  late double makeupGain;
  late double kneeWidth;
  late double ratio;
  late double attackTime;
  late double releaseTime;
  bool isFilterActive = false;

  @override
  void initState() {
    super.initState();

    wet = compressor.queryWet.def;
    threshold = compressor.queryThreshold.def;
    makeupGain = compressor.queryMakeupGain.def;
    kneeWidth = compressor.queryKneeWidth.def;
    ratio = compressor.queryRatio.def;
    attackTime = compressor.queryAttackTime.def;
    releaseTime = compressor.queryReleaseTime.def;
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
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text('Activate Compressor'),
                Checkbox(
                  value: isFilterActive,
                  onChanged: (value) {
                    if (value!) {
                      compressor.activate();
                      compressor.wet.value = wet;
                      compressor.threshold.value = threshold;
                      compressor.makeupGain.value = makeupGain;
                      compressor.kneeWidth.value = kneeWidth;
                      compressor.ratio.value = ratio;
                      compressor.attackTime.value = attackTime;
                      compressor.releaseTime.value = releaseTime;
                    } else {
                      compressor.deactivate();
                    }
                    setState(() {
                      isFilterActive = value;
                    });
                  },
                ),
              ],
            ),

            ElevatedButton(
              onPressed: () async {
                sound = await SoLoud.instance
                    .loadAsset('assets/audio/8_bit_mentality.mp3');
              },
              child: const Text('load'),
            ),

            ///
            ElevatedButton(
              onPressed: () {
                SoLoud.instance.play(sound!, looping: true);
                SoLoud.instance.play(sound!, looping: true);
                SoLoud.instance.play(sound!, looping: true);
                SoLoud.instance.play(sound!, looping: true);
                SoLoud.instance.play(sound!, looping: true);
                SoLoud.instance.play(sound!, looping: true);
              },
              child: const Text('play sound'),
            ),

            ///
            ElevatedButton(
              onPressed: () async {
                await SoLoud.instance.disposeAllSources();
              },
              child: const Text('stop all'),
            ),

            ///
            Row(
              children: [
                Text('Wet ${wet.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: wet,
                    min: compressor.queryWet.min,
                    max: compressor.queryWet.max,
                    onChanged: (value) {
                      setState(() {
                        wet = value;
                        compressor.wet.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Threshold ${threshold.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: threshold,
                    min: compressor.queryThreshold.min,
                    max: compressor.queryThreshold.max,
                    onChanged: (value) {
                      setState(() {
                        threshold = value;
                        compressor.threshold.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Makeup gain ${makeupGain.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: makeupGain,
                    min: compressor.queryMakeupGain.min,
                    max: compressor.queryMakeupGain.max,
                    onChanged: (value) {
                      setState(() {
                        makeupGain = value;
                        compressor.makeupGain.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Knee width ${kneeWidth.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: kneeWidth,
                    min: compressor.queryKneeWidth.min,
                    max: compressor.queryKneeWidth.max,
                    onChanged: (value) {
                      setState(() {
                        kneeWidth = value;
                        compressor.kneeWidth.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Ratio ${ratio.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: ratio,
                    min: compressor.queryRatio.min,
                    max: compressor.queryRatio.max,
                    onChanged: (value) {
                      setState(() {
                        ratio = value;
                        compressor.ratio.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Attack time ${attackTime.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: attackTime,
                    min: compressor.queryAttackTime.min,
                    max: compressor.queryAttackTime.max,
                    onChanged: (value) {
                      setState(() {
                        attackTime = value;
                        compressor.attackTime.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Release time ${releaseTime.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: releaseTime,
                    min: compressor.queryReleaseTime.min,
                    max: compressor.queryReleaseTime.max,
                    onChanged: (value) {
                      setState(() {
                        releaseTime = value;
                        compressor.releaseTime.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

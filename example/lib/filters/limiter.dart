import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows how to use the limiter filter.
/// 
/// Happens that playing multple sounds at the same time can create a lot of
/// noise and output distortion. The limiter filter can be used to reduce this
/// behaviour.
/// 
/// In this example we play a sound multiple times and adjust the limiter
/// parameters.
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
      home: LimiterExample(),
    ),
  );
}

/// Simple usecase of flutter_soloud plugin
class LimiterExample extends StatefulWidget {
  const LimiterExample({super.key});

  @override
  State<LimiterExample> createState() => _LimiterExampleState();
}

class _LimiterExampleState extends State<LimiterExample> {
  final limiter = SoLoud.instance.filters.limiterFilter;
  AudioSource? sound;
  late double wet;
  late double threshold;
  late double attackTime;
  late double releaseTime;
  late double makeupGain;
  bool isFilterActive = false;

  @override
  void initState() {
    super.initState();

    wet = limiter.queryWet.def;
    threshold = limiter.queryThreshold.def;
    attackTime = limiter.queryAttackTime.def;
    releaseTime = limiter.queryReleaseTime.def;
    makeupGain = limiter.queryMakeupGain.def;
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
            Checkbox(
              value: isFilterActive,
              onChanged: (value) {
                if (value!) {
                  limiter.activate();
                } else {
                  limiter.deactivate();
                }
                setState(() {
                  isFilterActive = value;
                });
              },
            ),

            ElevatedButton(
              onPressed: () async {
                sound = await SoLoud.instance
                    .loadAsset('assets/audio/explosion.mp3');
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
                    min: limiter.queryWet.min,
                    max: limiter.queryWet.max,
                    onChanged: (value) {
                      setState(() {
                        wet = value;
                        limiter.wet.value = value;
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
                    min: limiter.queryThreshold.min,
                    max: limiter.queryThreshold.max,
                    onChanged: (value) {
                      setState(() {
                        threshold = value;
                        limiter.threshold.value = value;
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
                    min: limiter.queryAttackTime.min,
                    max: limiter.queryAttackTime.max,
                    onChanged: (value) {
                      setState(() {
                        attackTime = value;
                        limiter.attackTime.value = value;
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
                    min: limiter.queryReleaseTime.min,
                    max: limiter.queryReleaseTime.max,
                    onChanged: (value) {
                      setState(() {
                        releaseTime = value;
                        limiter.releaseTime.value = value;
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
                    min: limiter.queryMakeupGain.min,
                    max: limiter.queryMakeupGain.max,
                    onChanged: (value) {
                      setState(() {
                        makeupGain = value;
                        limiter.makeupGain.value = value;
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

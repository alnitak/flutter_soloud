import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// This example shows the use of the limiter filter.
///
/// Happens that playing multple sounds at the same time can create a lot of
/// noise and output distortion. The limiter filter can be used to reduce this
/// behaviour.
///
/// In this example we play a sound multiple times and to check the sound and
/// adjust the limiter parameters.
///
/// ** Limiter parameters**:
/// - `wet`: Wet/dry mix ratio, 1.0 means fully wet, 0.0 means fully dry
/// - `threshold`: Sets the level (in dB) above which limiting is applied. Any
/// signal above this level is reduced in volume.
/// - `makeupGain`: Boosts the signal after limiting to make up for the volume
/// reduction. Measured in dB.
/// - `kneeWidth`: Controls the softness of the transition around the threshold.
/// A higher knee width results in a smoother, gradual limiting effect.
/// - `lookahead`: Allows the limiter to anticipate peaks by analyzing a certain
/// amount of samples ahead, helping to prevent clipping. Specified in
/// milliseconds (ms).
/// - `releaseTime`: Sets the time (in ms) over which the limiter recovers after
/// reducing gain, allowing for a smoother return to normal volume.
///
/// Tip: when lowering threshold the sound will get more distorted. Rise the
/// knee width to reduce the distortion.
/// Tip2: when using other filter, it's preferable to activate the limiter
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
  late double makeupGain;
  late double kneeWidth;
  late double lookahead;
  late double releaseTime;
  bool isFilterActive = false;

  @override
  void initState() {
    super.initState();

    wet = limiter.queryWet.def;
    threshold = limiter.queryThreshold.def;
    makeupGain = limiter.queryMakeupGain.def;
    kneeWidth = limiter.queryKneeWidth.def;
    lookahead = limiter.queryLookahead.def;
    releaseTime = limiter.queryReleaseTime.def;
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
                  limiter.wet.value = wet;
                  limiter.threshold.value = threshold;
                  limiter.makeupGain.value = makeupGain;
                  limiter.kneeWidth.value = kneeWidth;
                  limiter.lookahead.value = lookahead;
                  limiter.releaseTime.value = releaseTime;
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
            Row(
              children: [
                Text('Knee width ${kneeWidth.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: kneeWidth,
                    min: limiter.queryKneeWidth.min,
                    max: limiter.queryKneeWidth.max,
                    onChanged: (value) {
                      setState(() {
                        kneeWidth = value;
                        limiter.kneeWidth.value = value;
                      });
                    },
                  ),
                ),
              ],
            ),
            Row(
              children: [
                Text('Lookahead ${lookahead.toStringAsFixed(2)}'),
                Expanded(
                  child: Slider(
                    value: lookahead,
                    min: limiter.queryLookahead.min,
                    max: limiter.queryLookahead.max,
                    onChanged: (value) {
                      setState(() {
                        lookahead = value;
                        limiter.lookahead.value = value;
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
          ],
        ),
      ),
    );
  }
}

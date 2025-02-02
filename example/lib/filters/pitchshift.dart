import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';

/// Pitch shift example.
///
/// Filters can be accessed globally using `SoLoud.instance.filters` or for
/// single [AudioSource] sounds with `sound.filters`:
// ```
// /// For global filters.
// final ps = SoLoud.instance.filters.pitchShiftFilter;
// ps.activate();
// ps.shift(soundHandle: soundHandle).value = 0.6; // set value.
// final shift = ps.shift(soundHandle: soundHandle).value; // get value.
// ps.fadeFilterParameter(to: 3, time: const Duration(milliseconds: 2500));
// ps.oscillateFilterParameter(
//   from: 0.5,
//   to: 1.5,
//   time: const Duration(milliseconds: 2500),
// );
// ps.queryShift.[min | max | def] // to get filter min, max and default values.

// /// For single sound filters.
// final ps = sound.filters.pitchShiftFilter;
// [as for global filters]

// ```

/// Use the filter globally or attached to the sound. Filters for single sounds
/// are not supported in the Web platform.
const bool useGlobalFilter = false;

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
      home: PitchShift(),
    ),
  );
}

class PitchShift extends StatefulWidget {
  const PitchShift({super.key});

  @override
  State<PitchShift> createState() => _PitchShiftState();
}

class _PitchShiftState extends State<PitchShift> {
  final timeStretching = ValueNotifier<double>(1);
  final playSpeed = ValueNotifier<double>(1);
  final wet = ValueNotifier<double>(1);
  final shift = ValueNotifier<double>(1);
  final semitones = ValueNotifier<int>(0);

  AudioSource? sound;
  SoundHandle? soundHandle;

  @override
  void initState() {
    super.initState();
    try {
      SoLoud.instance
          .loadAsset('assets/audio/IveSeenThings.mp3')
          .then((value) async {
        sound = value;

        /// Add the filter to this sound handle before playing it.
        if (useGlobalFilter) {
          SoLoud.instance.filters.pitchShiftFilter.activate();
        } else {
          sound!.filters.pitchShiftFilter.activate();
        }

        /// start playing.
        soundHandle = await SoLoud.instance.play(sound!, looping: true);
      });
    } catch (e) {
      debugPrint(e.toString());
    }
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
                    'Pitch Shift Tempo example',
                    textScaler: TextScaler.linear(3),
                  ),
                  const SizedBox(height: 32),

                  /// Play Speed without changing the pitch.
                  ValueListenableBuilder<double>(
                    valueListenable: timeStretching,
                    builder: (_, ts, __) {
                      return Row(
                        children: [
                          Text('time stretching: ${ts.toStringAsFixed(2)}'),
                          Expanded(
                            child: Slider.adaptive(
                              min: 0.35,
                              max: 5,
                              value: ts,
                              onChanged: (value) {
                                if (soundHandle == null) return;
                                timeStretching.value = value;
                                playSpeed.value = value;
                                if (useGlobalFilter) {
                                  /// Using the filter for global, its possible
                                  /// to set manually the relative play speed
                                  /// and then set the shift relatively to
                                  /// the speed. The relation between speed and
                                  /// shift is shift = 1 / speed.
                                  /// But this implies that the shift is
                                  /// applied to all the sounds playing.

                                  // Adjust the play speed
                                  SoLoud.instance.setRelativePlaySpeed(
                                    soundHandle!,
                                    value,
                                  );

                                  // Adjust the pitchShift relatively to the
                                  // speed. The relation between speed and shift
                                  // is shift = 1 / speed.
                                  shift.value = 1 / value;
                                  SoLoud.instance.filters.pitchShiftFilter.shift
                                      .value = shift.value;
                                } else {
                                  /// Using the filter for a single sound, its
                                  /// possible to use the provided `timeStretch`
                                  /// method.
                                  sound!.filters.pitchShiftFilter
                                      .timeStretch(soundHandle!, value);
                                }
                              },
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 32),

                  /// Play Speed
                  ValueListenableBuilder<double>(
                    valueListenable: playSpeed,
                    builder: (_, speed, __) {
                      return Row(
                        children: [
                          Text('speed: ${(speed * 100).toInt()}%'),
                          Expanded(
                            child: Slider.adaptive(
                              max: 5,
                              value: speed,
                              onChanged: (value) {
                                if (soundHandle == null) return;
                                playSpeed.value = value;
                                SoLoud.instance
                                    .setRelativePlaySpeed(soundHandle!, value);
                              },
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 32),

                  /// Pitch Shift Wet parameter
                  ValueListenableBuilder<double>(
                    valueListenable: wet,
                    builder: (_, w, __) {
                      return Row(
                        children: [
                          Text('wet: ${w.toStringAsFixed(2)}'),
                          Expanded(
                            child: Slider.adaptive(
                              value: w,
                              onChanged: (value) {
                                if (soundHandle == null || sound == null) {
                                  return;
                                }
                                wet.value = value;
                                if (useGlobalFilter) {
                                  SoLoud.instance.filters.pitchShiftFilter.wet
                                      .value = w;
                                } else {
                                  sound!.filters.pitchShiftFilter
                                      .wet(soundHandle: soundHandle)
                                      .value = w;
                                }
                              },
                            ),
                          ),
                        ],
                      );
                    },
                  ),

                  /// Pitch Shift shift parameter
                  ValueListenableBuilder<double>(
                    valueListenable: shift,
                    builder: (_, s, __) {
                      return Row(
                        children: [
                          Text('shift: ${s.toStringAsFixed(2)}'),
                          Expanded(
                            child: Slider.adaptive(
                              value: s,
                              min: SoLoud.instance.filters.pitchShiftFilter
                                  .queryShift.min,
                              max: SoLoud.instance.filters.pitchShiftFilter
                                  .queryShift.max,
                              onChanged: (value) {
                                if (soundHandle == null || sound == null) {
                                  return;
                                }
                                shift.value = value;
                                if (useGlobalFilter) {
                                  SoLoud.instance.filters.pitchShiftFilter.shift
                                      .value = value;
                                } else {
                                  sound!.filters.pitchShiftFilter
                                      .shift(soundHandle: soundHandle)
                                      .value = value;
                                }

                                /// Changing the shift value also changes the
                                /// semitones. Update the semitones slider.
                                if (useGlobalFilter) {
                                  semitones.value = SoLoud.instance.filters
                                      .pitchShiftFilter.semitones.value
                                      .toInt()
                                      .clamp(
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .querySemitones.min
                                            .toInt(),
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .querySemitones.max
                                            .toInt(),
                                      );
                                } else {
                                  semitones.value = sound!
                                      .filters.pitchShiftFilter
                                      .semitones(soundHandle: soundHandle)
                                      .value
                                      .toInt()
                                      .clamp(
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .querySemitones.min
                                            .toInt(),
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .querySemitones.max
                                            .toInt(),
                                      );
                                }
                              },
                            ),
                          ),
                        ],
                      );
                    },
                  ),

                  /// Pitch Shift semitones parameter
                  ValueListenableBuilder<int>(
                    valueListenable: semitones,
                    builder: (_, s, __) {
                      return Row(
                        children: [
                          Text('semitones: $s'),
                          Expanded(
                            child: Slider.adaptive(
                              value: s.toDouble(),
                              min: SoLoud.instance.filters.pitchShiftFilter
                                  .querySemitones.min,
                              max: SoLoud.instance.filters.pitchShiftFilter
                                  .querySemitones.max,
                              divisions: 49,
                              onChanged: (value) {
                                if (soundHandle == null || sound == null) {
                                  return;
                                }
                                semitones.value = value.toInt();
                                if (useGlobalFilter) {
                                  SoLoud
                                      .instance
                                      .filters
                                      .pitchShiftFilter
                                      .semitones
                                      .value = semitones.value.toDouble();
                                } else {
                                  sound!.filters.pitchShiftFilter
                                      .semitones(soundHandle: soundHandle)
                                      .value = semitones.value.toDouble();
                                }

                                /// Changing the semitones value also the shift
                                /// changes. Update the shift slider.
                                if (useGlobalFilter) {
                                  shift.value = SoLoud.instance.filters
                                      .pitchShiftFilter.shift.value
                                      .clamp(
                                    SoLoud.instance.filters.pitchShiftFilter
                                        .queryShift.min,
                                    SoLoud.instance.filters.pitchShiftFilter
                                        .queryShift.max,
                                  );
                                } else {
                                  shift.value = sound!.filters.pitchShiftFilter
                                      .shift(soundHandle: soundHandle)
                                      .value
                                      .clamp(
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .queryShift.min,
                                        SoLoud.instance.filters.pitchShiftFilter
                                            .queryShift.max,
                                      );
                                }
                              },
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  const SizedBox(height: 32),

                  OutlinedButton(
                    onPressed: () {
                      if (soundHandle == null || sound == null) return;

                      /// Oscillate shift parameter from 0.4 to 1.6 in 1500ms
                      if (useGlobalFilter) {
                        SoLoud.instance.filters.pitchShiftFilter.shift
                            .oscillateFilterParameter(
                          from: 0.4,
                          to: 1.6,
                          time: const Duration(milliseconds: 1500),
                        );
                      } else {
                        sound!.filters.pitchShiftFilter
                            .shift(soundHandle: soundHandle)
                            .oscillateFilterParameter(
                              from: 0.4,
                              to: 1.6,
                              time: const Duration(milliseconds: 1500),
                            );
                      }
                    },
                    child: const Text('Oscillate shift'),
                  ),

                  OutlinedButton(
                    onPressed: () {
                      if (soundHandle == null || sound == null) return;

                      /// Fade shift parameter from the current value to 3.0
                      /// in 1500ms
                      if (useGlobalFilter) {
                        SoLoud.instance.filters.pitchShiftFilter.shift
                            .fadeFilterParameter(
                          to: 3,
                          time: const Duration(milliseconds: 1500),
                        );
                      } else {
                        sound!.filters.pitchShiftFilter
                            .shift(soundHandle: soundHandle)
                            .fadeFilterParameter(
                              to: 3,
                              time: const Duration(milliseconds: 1500),
                            );
                      }

                      /// Restore shift
                      Future.delayed(const Duration(milliseconds: 1500), () {
                        sound!.filters.pitchShiftFilter
                            .shift(soundHandle: soundHandle)
                            .value = 1;
                      });
                    },
                    child: const Text('Fade shift'),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }
}

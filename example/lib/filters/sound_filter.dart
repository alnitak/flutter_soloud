import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Pitch shift example.
///
/// In this example the filter is applied to a sound using the
/// [AudioSource.addFilter] before playing it (important). You can then use:
/// - [AudioSource.isFilterActive] to ask if enable.
/// - [AudioSource.removeFilter] to remove it.
/// - [AudioSource.setFilterParameter] to set a parameter.
/// - [AudioSource.getFilterParameter] to get a parameter.
/// - [AudioSource.fadeFilterParameter] to fade a parameter.
/// - [AudioSource.oscillateFilterParameter] to oscillate a parameter.
///
/// It is possible to apply filters globally instead of a single sound using
/// [SoLoud.addGlobalFilter] to add the filter.
/// [SoLoud.isFilterActive] to ask if enable.
/// [SoLoud.removeGlobalFilter] to remove it.
/// [SoLoud.setGlobalFilterParameter] to set a parameter.
/// [SoLoud.getGlobalFilterParameter] to get a parameter.
/// [SoLoud.fadeGlobalFilterParameter] to fade a parameter.
/// [SoLoud.oscillateGlobalVolume] to oscillate a parameter.
///
/// [SoLoud.getFilterParamNames] this method is the common way to get
/// parameter names for both global and sound filters.

void main() async {
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
          .loadAsset('assets/audio/8_bit_mentality.mp3')
          .then((value) async {
        sound = value;

        /// Add the filter to this sound handle before playing it.
        sound!.addFilter(FilterType.pitchShiftFilter);

        /// start playing.
        soundHandle = await SoLoud.instance.play(sound!, looping: true);
      });
    } catch (e) {
      debugPrint(e.toString());
    }
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

                  /// Play Speed
                  ValueListenableBuilder<double>(
                    valueListenable: playSpeed,
                    builder: (_, speed, __) {
                      return Row(
                        children: [
                          Text('speed: ${speed.toStringAsFixed(2)}'),
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
                                sound!.setFilterParameter(
                                  soundHandle!,
                                  FilterType.pitchShiftFilter,
                                  PitchShiftFilter.wet.attributeId,
                                  w,
                                );
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
                              max: 3,
                              onChanged: (value) {
                                if (soundHandle == null || sound == null) {
                                  return;
                                }
                                shift.value = value;
                                sound!.setFilterParameter(
                                  soundHandle!,
                                  FilterType.pitchShiftFilter,
                                  PitchShiftFilter.shift.attributeId,
                                  s,
                                );

                                /// Changing the shift value also the semitones
                                /// changes. Update the semitones slider.
                                semitones.value = sound!
                                    .getFilterParameter(
                                      soundHandle!,
                                      FilterType.pitchShiftFilter,
                                      PitchShiftFilter.semitones.attributeId,
                                    )
                                    .toInt()
                                    .clamp(-24, 24);
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
                              min: -24,
                              max: 24,
                              divisions: 49,
                              onChanged: (value) {
                                if (soundHandle == null || sound == null) {
                                  return;
                                }
                                semitones.value = value.toInt();
                                sound!.setFilterParameter(
                                  soundHandle!,
                                  FilterType.pitchShiftFilter,
                                  PitchShiftFilter.semitones.attributeId,
                                  s.toDouble(),
                                );

                                /// Changing the semitones value also the shift
                                /// changes. Update the shift slider.
                                shift.value = sound!
                                    .getFilterParameter(
                                      soundHandle!,
                                      FilterType.pitchShiftFilter,
                                      PitchShiftFilter.wet.attributeId,
                                    )
                                    .clamp(0, 3);
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
                      sound!.oscillateFilterParameter(
                        soundHandle!,
                        FilterType.pitchShiftFilter,
                        PitchShiftFilter.shift.attributeId,
                        0.4,
                        1.6,
                        const Duration(milliseconds: 1500),
                      );
                    },
                    child: const Text('Oscillate shift'),
                  ),

                  OutlinedButton(
                    onPressed: () {
                      if (soundHandle == null || sound == null) return;
                      sound!.fadeFilterParameter(
                        soundHandle!,
                        FilterType.pitchShiftFilter,
                        PitchShiftFilter.shift.attributeId,
                        3,
                        const Duration(milliseconds: 2500),
                      );

                      /// Restore shift
                      Future.delayed(const Duration(milliseconds: 2500), () {
                        sound!.setFilterParameter(
                          soundHandle!,
                          FilterType.pitchShiftFilter,
                          PitchShiftFilter.shift.attributeId,
                          1,
                        );
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

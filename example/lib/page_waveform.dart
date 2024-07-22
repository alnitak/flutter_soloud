import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/ui/bars.dart';
import 'package:flutter_soloud_example/ui/filter_fx.dart';
import 'package:flutter_soloud_example/ui/keyboard_widget.dart';
import 'package:flutter_soloud_example/ui/knobs_groups.dart';
import 'package:flutter_soloud_example/ui/text_slider.dart';
import 'package:star_menu/star_menu.dart';

/// Example to demostrate how waveforms work with a keyboard
///
class PageWaveform extends StatefulWidget {
  const PageWaveform({super.key});

  @override
  State<PageWaveform> createState() => _PageWaveformState();
}

class _PageWaveformState extends State<PageWaveform> {
  ValueNotifier<double> scale = ValueNotifier(0.25);
  ValueNotifier<double> detune = ValueNotifier(1);
  ValueNotifier<double> playSpeed = ValueNotifier(1);
  ValueNotifier<WaveForm> waveForm = ValueNotifier(WaveForm.fSquare);
  bool superWave = false;
  int octave = 2;
  double fadeIn = 0.3;
  double fadeOut = 0.3;
  double fadeSpeedIn = 0;
  double fadeSpeedOut = 0;
  double oscillateVol = 0;
  double oscillatePan = 0;
  double oscillateSpeed = 0;
  List<AudioSource> notes = [];
  AudioSource? sound;
  SoundHandle? soundHandle;
  bool canBuild = false;

  @override
  void initState() {
    super.initState();

    /// Only when the [notes] are set up, build the UI
    setupNotes().then((value) {
      if (context.mounted) {
        setState(() {
          canBuild = true;
        });
      }
    });
  }

  Future<void> setupNotes() async {
    await SoLoud.instance.disposeAllSources();
    notes = await SoLoudTools.createNotes(
      octave: octave,
      superwave: superWave,
      waveForm: waveForm.value,
    );

    /// set all sounds to pause state
    for (final s in notes) {
      await SoLoud.instance.play(s, paused: true);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized || !canBuild) {
      return const SizedBox.shrink();
    }

    return Scaffold(
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(8),
        child: Column(
          children: [
            Row(
              children: [
                ElevatedButton(
                  onPressed: () async {
                    if (sound != null) {
                      await SoLoud.instance.disposeSource(sound!);
                      sound = null;
                    }

                    await SoLoud.instance
                        .speechText('Hello Flutter Soloud!')
                        .then((value) => sound = value);
                  },
                  child: const Text('T2S'),
                ),
                const SizedBox(width: 8),
                ElevatedButton(
                  onPressed: () async {
                    sound = await SoLoud.instance.loadAsset(
                      'assets/audio/8_bit_mentality.mp3',
                    );
                    if (sound != null) {
                      soundHandle = await SoLoud.instance.play(sound!);
                    }
                  },
                  child: const Text('play sample'),
                ),
                const SizedBox(width: 8),
                ElevatedButton(
                  onPressed: () {
                    if (sound != null) {
                      SoLoud.instance.disposeSource(sound!).then((value) {
                        sound = null;
                        soundHandle = null;
                      });
                    }
                  },
                  child: const Text('stop'),
                ),
              ],
            ),

            /// Sample Play Speed
            ValueListenableBuilder<double>(
              valueListenable: playSpeed,
              builder: (_, speed, __) {
                return TextSlider(
                  text: 'sample speed  ',
                  min: 0.01,
                  max: 5,
                  value: speed,
                  enabled: true,
                  onChanged: (value) {
                    if (soundHandle == null) return;
                    playSpeed.value = value;
                    SoLoud.instance.setRelativePlaySpeed(soundHandle!, value);
                  },
                );
              },
            ),

            /// Scale
            ValueListenableBuilder<double>(
              valueListenable: scale,
              builder: (_, newScale, __) {
                return TextSlider(
                  text: 'scale  ',
                  min: 0,
                  max: 2,
                  value: newScale,
                  enabled: superWave,
                  onChanged: (value) {
                    scale.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud.instance.setWaveformScale(notes[i], value);
                    }
                  },
                );
              },
            ),

            /// Detune
            ValueListenableBuilder<double>(
              valueListenable: detune,
              builder: (_, newDetune, __) {
                return TextSlider(
                  text: 'detune',
                  min: 0,
                  max: 1,
                  value: newDetune,
                  enabled: superWave,
                  onChanged: (value) {
                    detune.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud.instance.setWaveformDetune(notes[i], value);
                    }
                  },
                );
              },
            ),

            /// Octave
            TextSlider(
              text: 'octave',
              min: 0,
              max: 4,
              value: octave.toDouble(),
              enabled: true,
              isDivided: true,
              onChanged: (value) async {
                octave = value.toInt();
                await setupNotes();
                if (mounted) setState(() {});
              },
            ),

            /// SuperWave
            Row(
              children: [
                const Text('superWave '),
                Checkbox.adaptive(
                  value: superWave,
                  onChanged: (value) {
                    superWave = value!;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud.instance.setWaveformSuperWave(notes[i], value);
                    }
                    if (mounted) setState(() {});
                  },
                ),
              ],
            ),

            DefaultTabController(
              length: 12,
              initialIndex: 2,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const TabBar(
                    isScrollable: true,
                    dividerColor: Colors.blue,
                    tabs: [
                      Tab(text: 'faders'),
                      Tab(text: 'oscillators'),
                      Tab(text: 'Pitch shift'),
                      Tab(text: 'Biquad Filter'),
                      Tab(text: 'Eq'),
                      Tab(text: 'Echo'),
                      Tab(text: 'Lofi'),
                      Tab(text: 'Flanger'),
                      Tab(text: 'Bassboost'),
                      Tab(text: 'Wave shaper'),
                      Tab(text: 'Robotize'),
                      Tab(text: 'Freeverb'),
                    ],
                  ),
                  const SizedBox(height: 8),
                  SizedBox(
                    height: 210,
                    child: TabBarView(
                      physics: const NeverScrollableScrollPhysics(),
                      children: [
                        /// Faders
                        KnobsGroup(
                          texts: const ['in', 'out', 'speed in', 'speed out'],
                          values: [fadeIn, fadeOut, fadeSpeedIn, fadeSpeedOut],
                          mins: const [0, 0, 0, 0],
                          maxs: const [2, 2, 2, 2],
                          onChanges: [
                            (value) => setState(() => fadeIn = value),
                            (value) => setState(() => fadeOut = value),
                            (value) => setState(() => fadeSpeedIn = value),
                            (value) => setState(() => fadeSpeedOut = value),
                          ],
                        ),

                        /// Oscillators
                        KnobsGroup(
                          texts: const ['volume', 'pan', 'speed'],
                          values: [oscillateVol, oscillatePan, oscillateSpeed],
                          mins: const [0, 0, 0],
                          maxs: const [0.5, 0.5, 0.5],
                          onChanges: [
                            (value) => setState(() => oscillateVol = value),
                            (value) => setState(() => oscillatePan = value),
                            (value) => setState(() => oscillateSpeed = value),
                          ],
                        ),

                        /// Pitch Shift
                        const FilterFx(
                          filterType: FilterType.pitchShiftFilter,
                        ),

                        /// Biquad Resonant
                        const FilterFx(
                          filterType: FilterType.biquadResonantFilter,
                        ),

                        /// Eq
                        const FilterFx(filterType: FilterType.eqFilter),

                        /// Echo
                        const FilterFx(filterType: FilterType.echoFilter),

                        /// Lofi
                        const FilterFx(filterType: FilterType.lofiFilter),

                        /// Flanger
                        const FilterFx(filterType: FilterType.flangerFilter),

                        /// Bassboost
                        const FilterFx(filterType: FilterType.bassboostFilter),

                        /// Wave Shaper
                        const FilterFx(filterType: FilterType.waveShaperFilter),

                        /// Robotize
                        const FilterFx(filterType: FilterType.robotizeFilter),

                        /// Freeverb
                        const FilterFx(filterType: FilterType.freeverbFilter),
                      ],
                    ),
                  ),
                ],
              ),
            ),

            /// Choose wave form
            StarMenu(
              params: StarMenuParameters(
                shape: MenuShape.linear,
                boundaryBackground: BoundaryBackground(
                  color: Colors.white.withOpacity(0.1),
                  blurSigmaX: 6,
                  blurSigmaY: 6,
                ),
                linearShapeParams: const LinearShapeParams(
                  angle: -90,
                  alignment: LinearAlignment.left,
                ),
              ),
              onItemTapped: (index, controller) {
                controller.closeMenu!();
              },
              items: [
                for (int i = 0; i < WaveForm.values.length; i++)
                  ActionChip(
                    backgroundColor: Colors.blue,
                    onPressed: () async {
                      waveForm.value = WaveForm.values[i];
                      await setupNotes();
                      if (mounted) setState(() {});
                    },
                    label: Text(WaveForm.values[i].name),
                  ),
              ],
              child: Chip(
                label: Text(WaveForm.values[waveForm.value.index].name),
                backgroundColor: Colors.blue,
                avatar: const Icon(Icons.arrow_drop_down),
              ),
            ),

            const SizedBox(height: 8),
            KeyboardWidget(
              notes: notes,
              fadeIn: _duration(fadeIn),
              fadeOut: _duration(fadeOut),
              fadeSpeedIn: _duration(fadeSpeedIn),
              fadeSpeedOut: _duration(fadeSpeedOut),
              oscillateVolume: _duration(oscillateVol),
              oscillatePan: _duration(oscillatePan),
              oscillateSpeed: _duration(oscillateSpeed),
            ),
            const SizedBox(height: 8),
            Bars(key: UniqueKey()),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }

  static Duration _duration(double seconds) => Duration(
        microseconds: (seconds * Duration.microsecondsPerSecond).round(),
      );
}

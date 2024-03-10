import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/waveform/bars.dart';
import 'package:flutter_soloud_example/waveform/filter_fx.dart';
import 'package:flutter_soloud_example/waveform/keyboard_widget.dart';
import 'package:flutter_soloud_example/waveform/knobs_groups.dart';
import 'package:flutter_soloud_example/waveform/text_slider.dart';
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
  List<SoundProps> notes = [];
  SoundProps? sound;

  @override
  void initState() {
    super.initState();

    /// listen to player events
    SoLoud.instance.audioEvent.stream.listen((event) async {
      if (event == AudioEvent.isolateStarted) {
        /// When it starts initialize notes
        SoLoud.instance.setVisualizationEnabled(true);
        await setupNotes();
        SoLoud.instance.setGlobalVolume(0.6);
      }
      if (mounted) setState(() {});
    });
    SoLoud.instance.initialize();
  }

  @override
  void dispose() {
    SoLoud.instance.shutdown();
    SoLoudCapture.instance.stopCapture();
    super.dispose();
  }

  Future<void> setupNotes() async {
    await SoLoud.instance.disposeAllSound();
    notes = await SoloudTools.initSounds(
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
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

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
                      await SoLoud.instance.disposeSound(sound!);
                    }

                    /// text created by ChatGPT :)
                    await SoLoud.instance
                        .speechText('Flutter and So Loud audio plugin are the '
                            "tech tag team you never knew you needed - they're "
                            'like Batman and Robin, swooping in to save your '
                            'app with style and sound effects that would make '
                            'even Gotham jealous!')
                        .then((value) => sound = value.sound);
                  },
                  child: const Text('T2S'),
                ),
                const SizedBox(width: 8),
                ElevatedButton(
                  onPressed: () async {
                    final ret = await SoloudTools.loadFromAssets(
                      'assets/audio/8_bit_mentality.mp3',
                    );
                    await SoLoud.instance
                        .play(ret!)
                        .then((value) => sound = value.sound);
                  },
                  child: const Text('play sample'),
                ),
                const SizedBox(width: 8),
                ElevatedButton(
                  onPressed: () {
                    if (sound != null) {
                      SoLoud.instance.disposeSound(sound!).then((value) {
                        sound = null;
                      });
                    }
                  },
                  child: const Text('stop'),
                ),
              ],
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
              length: 11,
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const TabBar(
                    isScrollable: true,
                    dividerColor: Colors.blue,
                    tabs: [
                      Tab(text: 'faders'),
                      Tab(text: 'oscillators'),
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
                linearShapeParams: LinearShapeParams(
                  angle: -90,
                  space: Platform.isAndroid || Platform.isIOS ? -10 : 10,
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
              fadeIn: fadeIn,
              fadeOut: fadeOut,
              fadeSpeedIn: fadeSpeedIn,
              fadeSpeedOut: fadeSpeedOut,
              oscillateVolume: oscillateVol,
              oscillatePan: oscillatePan,
              oscillateSpeed: oscillateSpeed,
            ),
            const SizedBox(height: 8),
            Bars(key: UniqueKey()),
            const SizedBox(height: 8),
          ],
        ),
      ),
    );
  }
}

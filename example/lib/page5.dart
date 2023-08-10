import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';
import 'package:star_menu/star_menu.dart';

class Page5 extends StatefulWidget {
  const Page5({super.key});

  @override
  State<Page5> createState() => _Page5State();
}

class _Page5State extends State<Page5> {
  ValueNotifier<double> scale = ValueNotifier(0.25);
  ValueNotifier<double> detune = ValueNotifier(1);
  ValueNotifier<double> freq = ValueNotifier(400);
  ValueNotifier<bool> superWave = ValueNotifier(false);
  ValueNotifier<WaveForm> waveForm = ValueNotifier(WaveForm.fSquare);
  ValueNotifier<int> octave = ValueNotifier(2);
  List<SoundProps> notes = [];

  @override
  void initState() {
    super.initState();
    SoLoud().audioEvent.stream.listen((event) async {
      if (event == AudioEvent.isolateStarted) {
        SoLoud().setVisualizationEnabled(true);
        notes = await SoloudTools.initSounds(
          octave: 1,
          superwave: superWave.value,
        );
      }
      if (mounted) setState(() {});
    });
    SoLoud().startIsolate();
  }

  @override
  void dispose() {
    SoLoud().stopIsolate();
    SoLoud().stopCapture();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud().isPlayerInited) return const SizedBox.shrink();

    return Scaffold(
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceAround,
          children: [
            /// Scale
            ValueListenableBuilder<double>(
              valueListenable: scale,
              builder: (_, newScale, __) {
                return MySlider(
                  text: 'scale  ',
                  min: 0,
                  max: 2,
                  value: newScale,
                  enabled: superWave.value,
                  onChanged: (value) {
                    scale.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud().setWaveformScale(
                        notes[i],
                        value,
                      );
                    }
                  },
                );
              },
            ),

            /// Detune
            ValueListenableBuilder<double>(
              valueListenable: detune,
              builder: (_, newDetune, __) {
                return MySlider(
                  text: 'detune',
                  min: 0,
                  max: 1,
                  value: newDetune,
                  enabled: superWave.value,
                  onChanged: (value) {
                    detune.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud().setWaveformDetune(
                        notes[i],
                        value,
                      );
                    }
                  },
                );
              },
            ),

            /// Freq
            ValueListenableBuilder<double>(
              valueListenable: freq,
              builder: (_, newFreq, __) {
                return MySlider(
                  text: 'freq',
                  min: 55,
                  max: 2000,
                  value: newFreq,
                  enabled: true,
                  onChanged: (value) {
                    freq.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud().setWaveformFreq(
                        notes[i],
                        value,
                      );
                    }
                  },
                );
              },
            ),

            /// Octave
            ValueListenableBuilder<int>(
              valueListenable: octave,
              builder: (_, newOctave, __) {
                return MySlider(
                  text: 'octave',
                  min: 0,
                  max: 4,
                  value: newOctave.toDouble(),
                  enabled: true,
                  isDivided: true,
                  onChanged: (value) async {
                    octave.value = value.toInt();
                    await SoLoud().disposeAllSound();
                    notes = await SoloudTools.initSounds(
                      octave: value.toInt(),
                      superwave: superWave.value,
                    );
                    if (mounted) setState(() {});
                  },
                );
              },
            ),

            /// SuperWave
            ValueListenableBuilder<bool>(
              valueListenable: superWave,
              builder: (_, newSuperWave, __) {
                return Row(
                  children: [
                    const Text('superWave '),
                    Checkbox.adaptive(
                      value: newSuperWave,
                      onChanged: (value) {
                        superWave.value = value!;
                        for (var i = 0; i < notes.length; i++) {
                          SoLoud().setWaveformSuperWave(
                            notes[i],
                            value,
                          );
                        }
                        if (mounted) setState(() {});
                      },
                    ),
                  ],
                );
              },
            ),

            /// Wave form
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
                    onPressed: () {
                      waveForm.value = WaveForm.values[i];
                      for (var n = 0; n < notes.length; n++) {
                        SoLoud().setWaveform(
                          notes[n],
                          WaveForm.values[i],
                        );
                      }
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
            KeyboardWidget(notes: notes),
            const Spacer(),
            Bars(key: UniqueKey()),
            const Spacer(),
          ],
        ),
      ),
    );
  }
}

class MySlider extends StatelessWidget {
  const MySlider({
    required this.text,
    required this.min,
    required this.max,
    required this.value,
    required this.enabled,
    required this.onChanged,
    this.isDivided = false,
    super.key,
  });

  final String text;
  final double min;
  final double max;
  final double value;
  final bool enabled;
  final bool isDivided;
  final void Function(double) onChanged;

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Text(
          '$text: ${isDivided ? value.toInt() : value.toStringAsFixed(2)}\t',
        ),
        Expanded(
          child: Slider.adaptive(
            value: value,
            min: min,
            max: max,
            divisions: isDivided ? 4 : null,
            onChanged: enabled ? onChanged : null,
          ),
        )
      ],
    );
  }
}

class Bars extends StatefulWidget {
  const Bars({super.key});

  @override
  State<Bars> createState() => BbarsState();
}

class BbarsState extends State<Bars> with SingleTickerProviderStateMixin {
  late final Ticker ticker;
  ffi.Pointer<ffi.Pointer<ffi.Float>> playerData = ffi.nullptr;

  @override
  void initState() {
    super.initState();
    playerData = calloc();
    ticker = createTicker(_tick);
    ticker.start();
  }

  @override
  void dispose() {
    ticker.stop();
    calloc.free(playerData);
    playerData = ffi.nullptr;
    super.dispose();
  }

  void _tick(Duration elapsed) {
    if (mounted) {
      SoLoud().getAudioTexture2D(playerData);
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(6),
      child: Row(
        children: [
          BarsFftWidget(
            audioData: playerData.value,
            minFreq: 0,
            maxFreq: 256,
            width: MediaQuery.sizeOf(context).width / 2 - 9,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
          const SizedBox(width: 6),
          BarsWaveWidget(
            audioData: playerData.value,
            width: MediaQuery.sizeOf(context).width / 2 - 9,
            height: MediaQuery.sizeOf(context).width / 6,
          ),
        ],
      ),
    );
  }
}

class KeyboardWidget extends StatefulWidget {
  const KeyboardWidget({
    required this.notes,
    super.key,
  });

  final List<SoundProps> notes;

  @override
  State<KeyboardWidget> createState() => _KeyboardWidgetState();
}

class _KeyboardWidgetState extends State<KeyboardWidget> {
  late final List<String> notesText;
  late final List<ValueNotifier<bool>> isPressed;
  final noteKeyWidth = 30;

  @override
  void initState() {
    super.initState();
    notesText = [
      'C',
      'C#',
      'D',
      'D#',
      'E',
      'F',
      'F#',
      'G',
      'G#',
      'A',
      'A#',
      'B',
    ];
    isPressed = List.generate(12, (index) {
      return ValueNotifier(false);
    });
  }

  void stopAll() {
    for (var i = 0; i < 12; i++) {
      if (widget.notes[i].handle.isNotEmpty) {
        SoLoud().stop(widget.notes[i].handle.first);
      }
      isPressed[i].value = false;
    }
  }

  void play(int index) {
    SoLoud().play(widget.notes[index], volume: 0.7);
    isPressed[index].value = true;
  }

  @override
  Widget build(BuildContext context) {
    if (widget.notes.length != 12) return const SizedBox.shrink();

    return SizedBox(
      height: 120,
      child: Listener(
        onPointerDown: (event) => playNote(event.localPosition.dx),
        onPointerMove: (event) => playNote(event.localPosition.dx),
        onPointerCancel: (e) => stopAll(),
        onPointerUp: (e) => stopAll(),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (int i = 0; i < 12; i++)
              ValueListenableBuilder<bool>(
                valueListenable: isPressed[i],
                builder: (_, pressed, __) {
                  return Container(
                    width: noteKeyWidth.toDouble(),
                    height: 120,
                    decoration: BoxDecoration(
                      color: pressed ? Colors.grey : Colors.white,
                      border: Border.all(
                        width: 2,
                      ),
                    ),
                    alignment: Alignment.bottomCenter,
                    child: Text(
                      notesText[i],
                      style: const TextStyle(
                        color: Colors.black,
                        fontWeight: FontWeight.w900,
                      ),
                    ),
                  );
                },
              ),
          ],
        ),
      ),
    );
  }

  void playNote(double x) {
    final noteOver = x.toInt() ~/ noteKeyWidth;
    if (!isPressed[noteOver].value) {
      stopAll();
      play(noteOver);
      isPressed[noteOver].value = true;
    }
  }
}

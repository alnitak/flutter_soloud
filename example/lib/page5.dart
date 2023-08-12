import 'dart:ffi' as ffi;
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';
import 'package:star_menu/star_menu.dart';

/// Example to demostrate how waveforms work with a keyboard
///
class Page5 extends StatefulWidget {
  const Page5({super.key});

  @override
  State<Page5> createState() => _Page5State();
}

class _Page5State extends State<Page5> {
  ValueNotifier<double> scale = ValueNotifier(0.25);
  ValueNotifier<double> detune = ValueNotifier(1);
  ValueNotifier<WaveForm> waveForm = ValueNotifier(WaveForm.fSquare);
  bool superWave = false;
  int octave = 2;
  double fadeIn = 0.5;
  double fadeOut = 0.5;
  double fadeSpeedIn = 0;
  double fadeSpeedOut = 0;
  List<SoundProps> notes = [];

  @override
  void initState() {
    super.initState();

    /// listen to player events
    SoLoud().audioEvent.stream.listen((event) async {
      if (event == AudioEvent.isolateStarted) {
        /// When it starts initialize notes
        SoLoud().setVisualizationEnabled(true);
        await setupNotes();
      }
      if (mounted) setState(() {});
    });
    SoLoud().startIsolate();
  }

  @override
  void dispose() {
    SoLoud().stopIsolate();
    SoLoud().stopCapture();
    SoLoud().disposeAllSound();
    super.dispose();
  }

  Future<void> setupNotes() async {
    await SoLoud().disposeAllSound();
    notes = await SoloudTools.initSounds(
      octave: 1,
      superwave: superWave,
      waveForm: waveForm.value,
    );

    /// set all sounds to pause state
    for (final s in notes) {
      await SoLoud().play(s, paused: true);
    }
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
                  enabled: superWave,
                  onChanged: (value) {
                    scale.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud().setWaveformScale(notes[i], value);
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
                  enabled: superWave,
                  onChanged: (value) {
                    detune.value = value;
                    for (var i = 0; i < notes.length; i++) {
                      SoLoud().setWaveformDetune(notes[i], value);
                    }
                  },
                );
              },
            ),

            /// Octave
            MySlider(
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

            /// Fade in
            MySlider(
              text: 'fade in',
              min: 0,
              max: 2,
              value: fadeIn,
              enabled: true,
              onChanged: (value) async {
                fadeIn = value;
                if (mounted) setState(() {});
              },
            ),

            /// Fade out
            MySlider(
              text: 'fade out',
              min: 0,
              max: 2,
              value: fadeOut,
              enabled: true,
              onChanged: (value) async {
                fadeOut = value;
                if (mounted) setState(() {});
              },
            ),

            /// Fade speed in
            MySlider(
              text: 'fade speed in',
              min: 0,
              max: 2,
              value: fadeSpeedIn,
              enabled: true,
              onChanged: (value) async {
                fadeSpeedIn = value;
                if (mounted) setState(() {});
              },
            ),

            /// Fade speed out
            MySlider(
              text: 'fade speed out',
              min: 0,
              max: 2,
              value: fadeSpeedOut,
              enabled: true,
              onChanged: (value) async {
                fadeSpeedOut = value;
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
                      SoLoud().setWaveformSuperWave(notes[i], value);
                    }
                    if (mounted) setState(() {});
                  },
                ),
              ],
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
                    onPressed: () {
                      waveForm.value = WaveForm.values[i];
                      setupNotes();
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
            ),
            const Spacer(),
            Bars(key: UniqueKey()),
            const Spacer(),
          ],
        ),
      ),
    );
  }
}

/// Custom slider with text
///
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

/// Visualizer for FFT and wave data
///
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

/// Widget to display and manage touch/keys event
///
class KeyboardWidget extends StatefulWidget {
  const KeyboardWidget({
    required this.notes,
    required this.fadeIn,
    required this.fadeOut,
    required this.fadeSpeedIn,
    required this.fadeSpeedOut,
    super.key,
  });

  final double fadeIn;
  final double fadeOut;
  final double fadeSpeedIn;
  final double fadeSpeedOut;
  final List<SoundProps> notes;

  @override
  State<KeyboardWidget> createState() => _KeyboardWidgetState();
}

class _KeyboardWidgetState extends State<KeyboardWidget> {
  late final List<String> notesText;
  late final List<String> notesKeys;
  late final List<ValueNotifier<bool>> isPressed;
  final noteKeyWidth = 30;
  int lastKeyPress = -1;

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
    notesKeys = [
      'Q',
      '2',
      'W',
      '3',
      'E',
      'R',
      '5',
      'T',
      '6',
      'Y',
      '7',
      'U',
    ];
    isPressed = List.generate(12, (index) {
      return ValueNotifier(false);
    });

    ServicesBinding.instance.keyboard.addHandler(onKey);
  }

  @override
  void dispose() {
    ServicesBinding.instance.keyboard.removeHandler(onKey);
    super.dispose();
  }

  /// Play a sound when key is pressed
  bool onKey(KeyEvent event) {
    final key = event.logicalKey.keyLabel;
    final keyId = notesKeys.indexOf(key);
    if (keyId == -1) return true;

    if (event is KeyDownEvent) {
      play(keyId);
    } else if (event is KeyUpEvent) {
      stop(keyId);
    }

    return false;
  }

  Future<void> play(int index) async {
    if (index < 0 || index >= notesKeys.length) return;
    if (isPressed[index].value) return;
    SoLoud().setRelativePlaySpeed(widget.notes[index].handle.first, 0);
    SoLoud().setPause(widget.notes[index].handle.first, false);
    SoLoud().fadeVolume(widget.notes[index].handle.first, 1, widget.fadeIn);
    SoLoud().fadeRelativePlaySpeed(
      widget.notes[index].handle.first,
      1,
      widget.fadeSpeedIn,
    );
    isPressed[index].value = true;
  }

  void stop(int index) {
    if (index < 0 || index >= notesKeys.length) return;
    for (final h in widget.notes[index].handle) {
      SoLoud().fadeVolume(h, 0, widget.fadeOut);
      SoLoud().fadeRelativePlaySpeed(h, 0, widget.fadeSpeedOut);
      SoLoud().schedulePause(h, widget.fadeOut);
    }
    isPressed[index].value = false;
  }

  @override
  Widget build(BuildContext context) {
    if (widget.notes.length != 12) return const SizedBox.shrink();

    return SizedBox(
      height: 120,
      child: Listener(
        onPointerDown: (e) {
          stop(lastKeyPress);
          lastKeyPress = e.localPosition.dx.toInt() ~/ noteKeyWidth;
          play(lastKeyPress);
        },
        onPointerMove: (e) {
          if (lastKeyPress != e.localPosition.dx.toInt() ~/ noteKeyWidth) {
            stop(lastKeyPress);
          }
          lastKeyPress = e.localPosition.dx.toInt() ~/ noteKeyWidth;
          play(lastKeyPress);
        },
        onPointerCancel: (e) {
          stop(lastKeyPress);
          lastKeyPress = -1;
        },
        onPointerUp: (e) {
          stop(lastKeyPress);
          lastKeyPress = -1;
        },
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
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Text(
                          notesKeys[i].toLowerCase(),
                          style: const TextStyle(
                            color: Colors.black,
                            fontWeight: FontWeight.w900,
                          ),
                        ),
                        Text(
                          notesText[i],
                          style: const TextStyle(
                            color: Colors.black,
                            fontWeight: FontWeight.w900,
                          ),
                        ),
                      ],
                    ),
                  );
                },
              ),
          ],
        ),
      ),
    );
  }
}

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Widget to display and manage touch/keys event
///
class KeyboardWidget extends StatefulWidget {
  const KeyboardWidget({
    required this.notes,
    required this.fadeIn,
    required this.fadeOut,
    required this.fadeSpeedIn,
    required this.fadeSpeedOut,
    required this.oscillateVolume,
    required this.oscillatePan,
    required this.oscillateSpeed,
    super.key,
  });

  final Duration fadeIn;
  final Duration fadeOut;
  final Duration fadeSpeedIn;
  final Duration fadeSpeedOut;
  final Duration oscillateVolume;
  final Duration oscillatePan;
  final Duration oscillateSpeed;
  final List<AudioSource> notes;

  @override
  State<KeyboardWidget> createState() => _KeyboardWidgetState();
}

class _KeyboardWidgetState extends State<KeyboardWidget> {
  late final List<String> notesText;
  late final List<String> notesKeys;
  late final List<ValueNotifier<bool>> isPressed;
  late double noteKeyWidth;
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
    final handle = widget.notes[index].handles.first;
    SoLoud.instance.setRelativePlaySpeed(handle, 0);
    SoLoud.instance.setVolume(handle, 0);
    SoLoud.instance.fadeVolume(handle, 1, widget.fadeIn);
    SoLoud.instance.fadeRelativePlaySpeed(
      handle,
      1,
      widget.fadeSpeedIn,
    );
    if (widget.oscillateVolume > Duration.zero) {
      SoLoud.instance.oscillateVolume(handle, 0.3, 1, widget.oscillateVolume);
    }
    if (widget.oscillatePan > Duration.zero) {
      SoLoud.instance.oscillatePan(handle, 0.3, 1, widget.oscillatePan);
    }
    if (widget.oscillateSpeed > Duration.zero) {
      SoLoud.instance
          .oscillateRelativePlaySpeed(handle, 0.3, 1, widget.oscillateSpeed);
    }
    SoLoud.instance.setPause(handle, false);
    isPressed[index].value = true;
  }

  void stop(int index) {
    if (index < 0 || index >= notesKeys.length) return;
    for (final h in widget.notes[index].handles) {
      SoLoud.instance.fadeVolume(h, 0, widget.fadeOut);
      SoLoud.instance.fadeRelativePlaySpeed(h, 0, widget.fadeSpeedOut);
      SoLoud.instance.schedulePause(h, widget.fadeOut);
    }
    isPressed[index].value = false;
  }

  @override
  Widget build(BuildContext context) {
    if (widget.notes.length != 12) return const SizedBox.shrink();
    noteKeyWidth = (MediaQuery.sizeOf(context).width - 16) / 12;

    return SizedBox(
      height: 120,
      child: GestureDetector(
        onPanDown: (e) {
          stop(lastKeyPress);
          lastKeyPress = e.localPosition.dx.toInt() ~/ noteKeyWidth;
          play(lastKeyPress);
        },
        onPanUpdate: (e) {
          if (lastKeyPress != e.localPosition.dx.toInt() ~/ noteKeyWidth) {
            stop(lastKeyPress);
          }
          lastKeyPress = e.localPosition.dx.toInt() ~/ noteKeyWidth;
          play(lastKeyPress);
        },
        onPanCancel: () {
          stop(lastKeyPress);
          lastKeyPress = -1;
        },
        onPanEnd: (e) {
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
                    width: noteKeyWidth,
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

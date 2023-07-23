import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/audio_isolate.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud/soloud_controller.dart';

class Controls extends StatefulWidget {
  const Controls({super.key});

  @override
  State<Controls> createState() => _ControlsState();
}

class _ControlsState extends State<Controls> {
  final isAudioIsolateRunning = ValueNotifier<bool>(false);
  final isCaptureRunning = ValueNotifier<bool>(false);

  ButtonStyle buttonStyle(bool enabled) {
    return enabled
        ? ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.green))
        : ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.red));
  }

  @override
  void initState() {
    super.initState();
  }

  @override
  void reassemble() {
    isAudioIsolateRunning.value = AudioIsolate().isIsolateRunning();
    super.reassemble();
  }

  @override
  Widget build(BuildContext context) {
    AudioIsolate().audioEvent.stream.listen(
      (event) {
        isAudioIsolateRunning.value = AudioIsolate().isIsolateRunning();
      },
    );

    return Row(
      children: [
        /// AudioIsolate
        ValueListenableBuilder<bool>(
          valueListenable: isAudioIsolateRunning,
          builder: (_, isRunning, __) {
            return ElevatedButton(
              onPressed: () async {
                if (isRunning) {
                  /// this will stop also the engine and the loop
                  final b = await AudioIsolate().stopIsolate();
                  if (b) {
                    debugPrint('isolate stopped');
                    isAudioIsolateRunning.value = false;
                  }
                } else {
                  final b = await AudioIsolate().startIsolate();
                  if (b == PlayerErrors.noError) {
                    debugPrint('isolate started');
                    unawaited(AudioIsolate().setVisualizationEnabled(true));
                    isAudioIsolateRunning.value = true;
                  }
                }
              },
              style: buttonStyle(isRunning),
              child: isRunning
                  ? const Text('stop player')
                  : const Text('start player'),
            );
          },
        ),
        const SizedBox(width: 16),

        /// Capture
        ValueListenableBuilder<bool>(
          valueListenable: isCaptureRunning,
          builder: (_, isRunning, __) {
            return ElevatedButton(
              onPressed: () async {
                if (isRunning) {
                  var a = AudioIsolate().stopCapture();
                  isCaptureRunning.value = false;
                } else {
                  var a = AudioIsolate().initCapture();
                  var b = AudioIsolate().startCapture();
                  isCaptureRunning.value = true;
                }
              },
              style: buttonStyle(isRunning),
              child: isRunning
                  ? const Text('stop capture')
                  : const Text('start capture'),
            );
          },
        ),
      ],
    );
  }
}

import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/audio_isolate.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';

class Controls extends StatefulWidget {
  const Controls({super.key});

  @override
  State<Controls> createState() => _ControlsState();
}

class _ControlsState extends State<Controls> {
  final isAudioIsolateRunning = ValueNotifier<bool>(false);

  ButtonStyle buttonStyle(bool enabled) {
    return enabled
        ? ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.green))
        : ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.red));
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

    return ValueListenableBuilder<bool>(
      valueListenable: isAudioIsolateRunning,
      builder: (_, isRunning, __) {
        return Column(
          children: [
            const SizedBox(height: 16),

            /// isolate start/stop   engine start/dispose
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                ElevatedButton(
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
                      ? const Text('stop isolate')
                      : const Text('start isolate'),
                ),
              ],
            ),
            const SizedBox(height: 32),
          ],
        );
      },
    );
  }
}

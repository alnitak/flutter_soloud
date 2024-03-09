import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:star_menu/star_menu.dart';

class Controls extends StatefulWidget {
  const Controls({super.key});

  @override
  State<Controls> createState() => _ControlsState();
}

class _ControlsState extends State<Controls> {
  static final Logger _log = Logger('_ControlsState');

  final isAudioIsolateRunning = ValueNotifier<bool>(false);
  final isCaptureRunning = ValueNotifier<bool>(false);
  late final List<CaptureDevice> captureDevices;
  int choosenCaptureDeviceId = -1;

  // ignore: avoid_positional_boolean_parameters
  ButtonStyle buttonStyle(bool enabled) {
    return enabled
        ? ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.green))
        : ButtonStyle(backgroundColor: MaterialStateProperty.all(Colors.red));
  }

  @override
  void initState() {
    super.initState();
    captureDevices = SoLoudCapture.instance.listCaptureDevices();
  }

  @override
  void reassemble() {
    isAudioIsolateRunning.value = SoLoud.instance.isIsolateRunning();
    isCaptureRunning.value = SoLoudCapture.instance.isCaptureStarted();
    super.reassemble();
  }

  @override
  Widget build(BuildContext context) {
    // TODO(filiph): remove this listener - it starts listening on every build
    SoLoud.instance.audioEvent.stream.listen(
      (event) {
        isAudioIsolateRunning.value = SoLoud.instance.isIsolateRunning();
        isCaptureRunning.value = SoLoudCapture.instance.isCaptureStarted();
      },
    );

    return Column(
      children: [
        Row(
          children: [
            /// AudioIsolate
            ValueListenableBuilder<bool>(
              valueListenable: isAudioIsolateRunning,
              builder: (_, isRunning, __) {
                return ElevatedButton(
                  onPressed: () async {
                    if (isRunning) {
                      /// this will stop also the engine and the loop
                      final b = await SoLoud.instance.dispose();
                      if (b) {
                        _log.info('isolate stopped');
                      }
                    } else {
                      final b = await SoLoud.instance.initialize();
                      if (b == PlayerErrors.noError) {
                        _log.info('isolate started');
                        SoLoud.instance.setVisualizationEnabled(true);
                      }
                    }
                  },
                  style: buttonStyle(isRunning),
                  child: const Text('player'),
                );
              },
            ),
            const SizedBox(width: 8),

            /// Capture
            ValueListenableBuilder<bool>(
              valueListenable: isCaptureRunning,
              builder: (_, isRunning, __) {
                return ElevatedButton(
                  onPressed: () async {
                    /// Ask recording permission on mobile
                    if (Platform.isAndroid || Platform.isIOS) {
                      final p = await Permission.microphone.isGranted;
                      if (!p) {
                        unawaited(Permission.microphone.request());
                        return;
                      }
                    }
                    if (isRunning) {
                      SoLoudCapture.instance.stopCapture();
                    } else {
                      SoLoudCapture.instance
                          .initialize(deviceID: choosenCaptureDeviceId);
                      SoLoudCapture.instance.startCapture();
                    }
                  },
                  style: buttonStyle(isRunning),
                  child: const Text('capture'),
                );
              },
            ),
            const SizedBox(width: 8),

            /// capture devices
            StarMenu(
              params: StarMenuParameters(
                shape: MenuShape.linear,
                centerOffset: const Offset(0, 50),
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
                setState(() {});
              },
              lazyItems: captureDeviceList,
              child: const Chip(
                label: Text('capture devices'),
                backgroundColor: Colors.blue,
                avatar: Icon(Icons.arrow_drop_down),
              ),
            ),
          ],
        ),
        const SizedBox(height: 6),
        Text(
          choosenCaptureDeviceId == -1
              ? 'using default capture device'
              : captureDevices[choosenCaptureDeviceId].name,
        ),
      ],
    );
  }

  Future<List<Widget>> captureDeviceList() async {
    final ret = List<Widget>.generate(captureDevices.length, (index) {
      final text = (captureDevices[index].isDefault ? 'DEF ' : '') +
          captureDevices[index].name;
      return Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          ElevatedButton(
            onPressed: () {
              choosenCaptureDeviceId = index;
            },
            style: buttonStyle(
              choosenCaptureDeviceId == index ||
                  (choosenCaptureDeviceId == -1 &&
                      captureDevices[index].isDefault),
            ),
            child: Text(text),
          ),
        ],
      );
    });

    return ret;
  }
}

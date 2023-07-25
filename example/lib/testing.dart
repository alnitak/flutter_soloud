import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/soloud_controller.dart';
import 'package:permission_handler/permission_handler.dart';

class Capture extends StatefulWidget {
  const Capture({super.key});

  @override
  State<Capture> createState() => _HelloFlutterSoLoudState();
}

class _HelloFlutterSoLoudState extends State<Capture> {
  final soLoudController = SoLoudController();

  @override
  void initState() {
    super.initState();
  }

  @override
  void dispose() {
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child:

            /// init
            Column(
          children: [
            if (Platform.isAndroid || Platform.isIOS || Platform.isMacOS)
              ElevatedButton(
                onPressed: () {
                  Permission.microphone
                    ..request().then((value) => print('PERMISSION $value'));
                },
                child: const Text('record permission'),
              ),

            ElevatedButton(
              onPressed: () async {
                final b = soLoudController.captureFFI.initCapture(6);
                print('INIT: $b');
              },
              child: const Text('init capture'),
            ),

            /// init
            ElevatedButton(
              onPressed: () async {
                soLoudController.captureFFI.startCapture();
              },
              child: const Text('start'),
            ),

            /// init
            ElevatedButton(
              onPressed: () async {
                soLoudController.captureFFI.stopCapture();
              },
              child: const Text('stop'),
            ),

            /// init
            ElevatedButton(
              onPressed: () async {
                final b = soLoudController.captureFFI.isCaptureInited();
                print('Capture is inited: $b');
              },
              child: const Text('is inited'),
            ),
          ],
        ),
      ),
    );
  }
}

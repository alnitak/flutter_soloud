import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:typed_data';

import 'package:ffi/ffi.dart';
import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Simple usecase of flutter_soloud plugin
class PageHelloFlutterSoLoud extends StatefulWidget {
  const PageHelloFlutterSoLoud({super.key});

  @override
  State<PageHelloFlutterSoLoud> createState() => _PageHelloFlutterSoLoudState();
}

class _PageHelloFlutterSoLoudState extends State<PageHelloFlutterSoLoud> {
  SoundProps? currentSound;

  @override
  void dispose() {
    SoLoud().stopIsolate();
    SoLoud().stopCapture();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.spaceAround,
          children: [
            /// pick audio file
            ElevatedButton(
              onPressed: () async {
                final paths = (await FilePicker.platform.pickFiles(
                  type: FileType.custom,
                  allowedExtensions: ['mp3', 'wav', 'ogg', 'flac'],
                  onFileLoading: print,
                  dialogTitle: 'Pick audio file',
                ))
                    ?.files;
                if (paths != null) {
                  unawaited(play(paths.first.path!));
                }
              },
              child: const Text('pick audio'),
            ),
            Column(
              children: [
                /// start the capture
                ElevatedButton(
                  onPressed: () async {
                    final a = SoLoud().initCapture();
                    final b = SoLoud().startCapture();
                    if (mounted &&
                        a == CaptureErrors.captureNoError &&
                        b == CaptureErrors.captureNoError) {
                      setState(() {});
                    }
                  },
                  child: const Text('start mic'),
                ),
                const SizedBox(height: 16),
                if (SoLoud().isCaptureInited)
                  const MicAudioWidget(
                    width: 100,
                    height: 100,
                  ),
              ],
            ),
          ],
        ),
      ),
    );
  }

  /// play file
  Future<void> play(String file) async {
    /// Start audio engine if not already
    if (!SoLoud().isIsolateRunning()) {
      await SoLoud().startIsolate().then((value) {
        if (value == PlayerErrors.noError) {
          debugPrint('isolate started');
        } else {
          debugPrint('isolate starting error: $value');
          return;
        }
      });
    }

    /// stop any previous sound loaded
    if (currentSound != null) {
      if (await SoLoud().disposeSound(currentSound!) != PlayerErrors.noError) {
        return;
      }
    }

    /// load the audio file
    final newSound = await SoloudTools.loadFromFile(file);
    if (newSound == null) return;
    currentSound = newSound;

    /// play it
    final playRet = await SoLoud().play(currentSound!);
    if (playRet.error != PlayerErrors.noError) return;
    currentSound = playRet.sound;
  }
}

/// widget that uses a ticker to read and provide audio
/// data to [MicAudioPainter]
///
class MicAudioWidget extends StatefulWidget {
  const MicAudioWidget({
    required this.width,
    required this.height,
    super.key,
  });
  final double width;
  final double height;

  @override
  State<MicAudioWidget> createState() => _MicAudioWidgetState();
}

class _MicAudioWidgetState extends State<MicAudioWidget>
    with SingleTickerProviderStateMixin {
  late Ticker ticker;
  late ffi.Pointer<ffi.Pointer<ffi.Float>> audioData;

  @override
  void initState() {
    super.initState();
    audioData = calloc();
    SoLoud().getCaptureAudioTexture2D(audioData);
    ticker = createTicker((Duration elapsed) {
      if (mounted) {
        SoLoud().getCaptureAudioTexture2D(audioData);
        setState(() {});
      }
    });
    ticker.start();
  }

  @override
  void dispose() {
    ticker.stop();
    calloc.free(audioData);
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: CustomPaint(
        size: Size(widget.width, widget.height),
        painter: MicAudioPainter(audioData: audioData),
      ),
    );
  }
}

/// Custom painter to draw the wave in a circle
///
class MicAudioPainter extends CustomPainter {
  const MicAudioPainter({
    required this.audioData,
  });
  final ffi.Pointer<ffi.Pointer<ffi.Float>> audioData;

  @override
  void paint(Canvas canvas, Size size) {
    final path = Path();

    /// draw background circle
    canvas.drawCircle(
      Offset(size.width / 2, size.height / 2),
      size.height / 2,
      Paint()
        ..color = Colors.blue
        ..style = PaintingStyle.fill,
    );

    /// simplify the first row of 256 FFT data to
    final data = Float64List(32);
    for (var n = 0; n < 32; n++) {
      var f = 0.0;
      for (var i = 0; i < 8; i++) {
        /// audioData[n * 8 + i]  is the FFT data
        /// If you want wave data, add 256 to the index
        f += audioData.value[n * 8 + i + 256];
      }
      data[n] = f / 8;
    }

    final stepX = size.width / 32;
    path.moveTo(0, (size.height / 2) + data[0] * size.height);
    for (var n = 1; n < 32; n++) {
      path.lineTo(
        n * stepX,
        (size.height / 2) + data[n] * size.height,
      );
    }
    canvas.drawPath(
      path,
      Paint()
        ..color = Colors.white
        ..style = PaintingStyle.stroke
        ..strokeWidth = 4,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) {
    return true;
  }
}

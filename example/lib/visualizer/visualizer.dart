// ignore_for_file: avoid_positional_boolean_parameters

import 'dart:async';
import 'dart:ui' as ui;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/audio_shader.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';
import 'package:flutter_soloud_example/visualizer/bmp_header.dart';
import 'package:flutter_soloud_example/visualizer/paint_texture.dart';

class VisualizerController extends ChangeNotifier {
  VisualizerController({
    this.isVisualizerEnabled = true,
    this.isVisualizerForPlayer = true,
    this.samplesKind = GetSamplesKind.texture,
  })  : maxRangeLimit = samplesKind == GetSamplesKind.texture ? 511 : 255,
        maxRange = samplesKind == GetSamplesKind.texture ? 511 : 255,
        minRange = 0 {
    audioData = AudioData(samplesKind);
  }

  int maxRangeLimit;
  int minRange;
  int maxRange;
  bool isVisualizerEnabled;
  bool isVisualizerForPlayer;
  GetSamplesKind samplesKind;
  late AudioData audioData;

  void changeMin(int min, {bool notify = true}) {
    minRange = min.clamp(0, maxRange);
    if (notify) {
      notifyListeners();
    }
  }

  void changeMax(int max, {bool notify = true}) {
    final nMax = samplesKind == GetSamplesKind.texture ? 511 : 255;
    maxRange = max.clamp(minRange, nMax);
    if (notify) {
      notifyListeners();
    }
  }

  void changeIsVisualizerForPlayer(bool isForPlayer) {
    isVisualizerForPlayer = isForPlayer;
    audioData.dispose();
    audioData = AudioData(samplesKind);
    notifyListeners();
  }

  void changeIsVisualizerEnabled(bool enable) {
    isVisualizerEnabled = enable;
    notifyListeners();
    SoLoud.instance.setVisualizationEnabled(enable);
  }

  void changeSamplesKind(GetSamplesKind kind) {
    samplesKind = kind;
    switch (kind) {
      case GetSamplesKind.linear:
        changeMin(0, notify: false);
        changeMax(255, notify: false);
        maxRangeLimit = 255;
      case GetSamplesKind.texture:
        changeMin(0, notify: false);
        changeMax(511, notify: false);
        maxRangeLimit = 511;
      case GetSamplesKind.wave:
        changeMin(0, notify: false);
        changeMax(255, notify: false);
        maxRangeLimit = 255;
    }
    audioData.changeSamplesKind(samplesKind);
    notifyListeners();
  }
}

class Visualizer extends StatefulWidget {
  const Visualizer({
    required this.controller,
    required this.shader,
    super.key,
  });

  final VisualizerController controller;
  final String shader;

  @override
  State<Visualizer> createState() => _VisualizerState();
}

class _VisualizerState extends State<Visualizer> with TickerProviderStateMixin {
  late Ticker ticker;
  late Stopwatch sw;
  late Bmp32Header image;
  late int bitmapRange;
  late Future<ui.Image?> Function() buildImageCallback;
  late int Function(SampleRow row, SampleColumn col) textureTypeCallback;
  int nFrames = 0;

  @override
  void initState() {
    super.initState();

    ticker = createTicker(_tick);
    sw = Stopwatch();
    sw.start();
    setupBitmapSize();
    ticker.start();

    widget.controller.addListener(() {
      ticker
        ..stop()
        ..dispose();
      ticker = createTicker(_tick);
      setupBitmapSize();
      ticker.start();
      sw.reset();
      nFrames = 0;
    });
  }

  @override
  void dispose() {
    ticker
      ..stop()
      ..dispose();
    sw.stop();
    super.dispose();
  }

  void _tick(Duration elapsed) {
    nFrames++;
    if (context.mounted) {
      try {
        widget.controller.audioData.updateSamples();
        setState(() {});
      } on Exception catch (e) {
        debugPrint('$e');
      }
    }
  }

  // @override
  // Widget build(BuildContext context) {
  //   return Row(
  //     children: [
  //       Column(
  //         children: [
  //           const Text(
  //             'FFT data',
  //             style: TextStyle(fontWeight: FontWeight.bold),
  //           ),

  //           /// FFT bars
  //           BarsFftWidget(
  //             audioData: widget.controller.audioData,
  //             minFreq: widget.controller.minRange,
  //             maxFreq: widget.controller.maxRange,
  //             width: 250,
  //             height: 120,
  //           ),
  //         ],
  //       ),
  //       const SizedBox(width: 6),
  //       Column(
  //         children: [
  //           const Text(
  //             '256 wave data',
  //             style: TextStyle(fontWeight: FontWeight.bold),
  //           ),

  //           /// wave data bars
  //           BarsWaveWidget(
  //             audioData: widget.controller.audioData,
  //             width: 250,
  //             height: 120,
  //           ),
  //         ],
  //       ),
  //     ],
  //   );
  // }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<ui.Image?>(
      future: buildImageCallback(),
      builder: (context, dataTexture) {
        final fps = nFrames.toDouble() / (sw.elapsedMilliseconds / 1000.0);
        if (!dataTexture.hasData || dataTexture.data == null) {
          return const Placeholder(
            color: Colors.red,
            strokeWidth: 0.5,
            child: Text("\n  can't get audio samples  \n"),
          );
        }

        return LayoutBuilder(
          builder: (context, constraints) {
            return Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'FPS: ${fps.toStringAsFixed(1)}     '
                  'the texture sent to the shader',
                  style: const TextStyle(fontWeight: FontWeight.bold),
                ),

                /// paint texture passed to the shader
                DisableButton(
                  width: constraints.maxWidth,
                  height: constraints.maxWidth / 6,
                  onPressed: () {
                    sw.reset();
                    nFrames = 0;
                  },
                  child: PaintTexture(
                    width: constraints.maxWidth,
                    height: constraints.maxWidth / 6,
                    image: dataTexture.data!,
                  ),
                ),

                const Text(
                  'SHADER',
                  style: TextStyle(fontWeight: FontWeight.bold),
                ),
                DisableButton(
                  width: constraints.maxWidth,
                  height: constraints.maxWidth / 2.4,
                  onPressed: () {
                    sw.reset();
                    nFrames = 0;
                  },
                  child: FutureBuilder<ui.FragmentShader?>(
                    future: loadShader(),
                    builder: (context, snapshot) {
                      if (snapshot.hasData) {
                        return AudioShader(
                          width: constraints.maxWidth,
                          height: constraints.maxWidth / 2.4,
                          image: dataTexture.data!,
                          shader: snapshot.data!,
                          iTime: sw.elapsedMilliseconds / 1000.0,
                        );
                      } else {
                        if (snapshot.data == null) {
                          return const Placeholder(
                            child: Align(
                              child: Text('Error compiling shader.\nSee log'),
                            ),
                          );
                        }
                        return const CircularProgressIndicator();
                      }
                    },
                  ),
                ),

                Row(
                  children: [
                    Column(
                      children: [
                        const Text(
                          'FFT data',
                          style: TextStyle(fontWeight: FontWeight.bold),
                        ),

                        /// FFT bars
                        DisableButton(
                          width: constraints.maxWidth / 2 - 3,
                          height: constraints.maxWidth / 6,
                          onPressed: () {
                            sw.reset();
                            nFrames = 0;
                          },
                          child: BarsFftWidget(
                            audioData: widget.controller.audioData,
                            minFreq: widget.controller.minRange,
                            maxFreq: widget.controller.maxRange,
                            width: constraints.maxWidth / 2 - 3,
                            height: constraints.maxWidth / 6,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(width: 6),
                    Column(
                      children: [
                        const Text(
                          '256 wave data',
                          style: TextStyle(fontWeight: FontWeight.bold),
                        ),

                        /// wave data bars
                        DisableButton(
                          width: constraints.maxWidth / 2 - 3,
                          height: constraints.maxWidth / 6,
                          onPressed: () {
                            sw.reset();
                            nFrames = 0;
                          },
                          child: BarsWaveWidget(
                            audioData: widget.controller.audioData,
                            width: constraints.maxWidth / 2 - 3,
                            height: constraints.maxWidth / 6,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ],
            );
          },
        );
      },
    );
  }

  /// load asynchronously the fragment shader
  Future<ui.FragmentShader?> loadShader() async {
    try {
      final program = await ui.FragmentProgram.fromAsset(widget.shader);
      return program.fragmentShader();
    } catch (e) {
      debugPrint('error compiling the shader $e');
    }
    return null;
  }

  void setupBitmapSize() {
    bitmapRange = widget.controller.maxRange - widget.controller.minRange + 1;

    switch (widget.controller.samplesKind) {
      case GetSamplesKind.wave:
        {
          image = Bmp32Header.setHeader(bitmapRange, 1);
          buildImageCallback = buildImageForWave;
          break;
        }
      case GetSamplesKind.linear:
        {
          image = Bmp32Header.setHeader(bitmapRange, 2);
          buildImageCallback = buildImageForLinear;
          break;
        }
      case GetSamplesKind.texture:
        {
          image = Bmp32Header.setHeader(bitmapRange, 256);
          buildImageCallback = buildImageForTexture;
          break;
        }
    }
  }

  /// Build an image to be passed to the shader.
  /// The image is a matrix of 256x1 RGBA pixels representing the wave data.
  Future<ui.Image?> buildImageForWave() async {
    if (!context.mounted) {
      return null;
    }
    if (!(widget.controller.isVisualizerEnabled &&
        SoLoud.instance.getVoiceCount() > 0)) {
      return null;
    }

    final completer = Completer<ui.Image>();
    final bytes = Uint8List(bitmapRange * 4);
    // Fill the texture bitmap
    var col = 0;
    for (var i = widget.controller.minRange;
        i <= widget.controller.maxRange;
        ++i, ++col) {
      // fill bitmap row with wave data
      final z = getWave(SampleWave(i));
      bytes[col * 4 + 0] = z;
      bytes[col * 4 + 1] = 0;
      bytes[col * 4 + 2] = 0;
      bytes[col * 4 + 3] = 255;
    }

    final img = image.storeBitmap(bytes);
    ui.decodeImageFromList(img, completer.complete);

    return completer.future;
  }

  /// Build an image to be passed to the shader.
  /// The image is a matrix of 256x2 RGBA pixels representing:
  /// in the 1st row the frequencies data
  /// in the 2nd row the wave data
  Future<ui.Image?> buildImageForLinear() async {
    if (!context.mounted) {
      return null;
    }
    if (!(widget.controller.isVisualizerEnabled &&
        SoLoud.instance.getVoiceCount() > 0)) {
      return null;
    }

    final completer = Completer<ui.Image>();
    final bytes = Uint8List(bitmapRange * 4 * 2);
    var col = 0;
    // Fill the texture bitmap
    for (var i = widget.controller.minRange;
        i <= widget.controller.maxRange;
        ++i, ++col) {
      // fill 1st bitmap row with FFT magnitude
      bytes[col * 4 + 0] = getLinearFft(SampleLinear(i));
      bytes[col * 4 + 1] = 0;
      bytes[col * 4 + 2] = 0;
      bytes[col * 4 + 3] = 255;
      // fill 2nd bitmap row with wave amplitudes
      bytes[col * 4 + 256 * 4 + 0] = getLinearWave(SampleLinear(i));
      bytes[col * 4 + 256 * 4 + 1] = 0;
      bytes[col * 4 + 256 * 4 + 2] = 0;
      bytes[col * 4 + 256 * 4 + 3] = 255;
    }

    final img = image.storeBitmap(bytes);
    ui.decodeImageFromList(img, completer.complete);

    return completer.future;
  }

  /// Build an image to be passed to the shader.
  /// The image is a matrix of 256x256 RGBA pixels representing
  /// rows of wave data or frequencies data.
  Future<ui.Image?> buildImageForTexture() async {
    if (!context.mounted) {
      return null;
    }
    if (!(widget.controller.isVisualizerEnabled &&
        SoLoud.instance.getVoiceCount() > 0)) {
      return null;
    }

    final width = widget.controller.maxRange - widget.controller.minRange;

    /// On the web there are worst performance getting data because for every
    /// single data a JS function must be called.
    /// Setting here an height of 100 instead of 256 to improve.
    const height = kIsWeb ? 100 : 256;

    final completer = Completer<ui.Image>();
    final bytes = Uint8List(width * height * 4);

    // Fill the texture bitmap with wave data
    var row = 0;
    for (var y = 0; y < height; ++y, ++row) {
      var col = 0;
      for (var x = 0; x < width; ++x, ++col) {
        final z = getTexture(SampleRow(y), SampleColumn(x));
        bytes[row * width * 4 + col * 4 + 0] = z;
        bytes[row * width * 4 + col * 4 + 1] = 0;
        bytes[row * width * 4 + col * 4 + 2] = 0;
        bytes[row * width * 4 + col * 4 + 3] = 255;
      }
    }

    image = Bmp32Header.setHeader(width, height);
    final img = image.storeBitmap(bytes);
    ui.decodeImageFromList(img, completer.complete);
    // final ui.Codec codec = await ui.instantiateImageCodec(img);
    // final ui.FrameInfo frameInfo = await codec.getNextFrame();
    // completer.complete(frameInfo.image);

    return completer.future;
  }

  int getWave(SampleWave offset) {
    final n = widget.controller.audioData.getWave(offset);
    return (((n + 1.0) / 2.0).clamp(0, 1) * 128).toInt();
  }

  int getLinearFft(SampleLinear offset) {
    return (widget.controller.audioData.getLinearFft(offset).clamp(0, 1) * 255)
        .toInt();
  }

  int getLinearWave(SampleLinear offset) {
    final n = widget.controller.audioData.getLinearWave(offset).abs();
    return (((n + 1.0) / 2.0).clamp(0, 1) * 128).toInt();
  }

  int getTexture(SampleRow row, SampleColumn col) {
    final n = widget.controller.audioData.getTexture(row, col);

    /// With col<256 we are asking for FFT values.
    if (col.value < 256) return (n.clamp(0, 1) * 255).toInt();

    /// With col>256 we are asking for wave values.
    return (((n + 1.0) / 2.0).clamp(0, 1) * 128).toInt();
  }
}

class DisableButton extends StatefulWidget {
  const DisableButton({
    required this.width,
    required this.height,
    required this.child,
    required this.onPressed,
    super.key,
  });

  final VoidCallback onPressed;
  final double width;
  final double height;
  final Widget child;

  @override
  State<DisableButton> createState() => _DisableButtonState();
}

class _DisableButtonState extends State<DisableButton> {
  late bool isChildVisible;

  @override
  void initState() {
    super.initState();
    isChildVisible = true;
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: widget.width,
      height: widget.height,
      child: Stack(
        children: [
          if (isChildVisible) widget.child else const Placeholder(),
          Align(
            alignment: Alignment.topRight,
            child: FloatingActionButton.small(
              onPressed: () {
                isChildVisible = !isChildVisible;
                setState(() {});
                widget.onPressed();
              },
              backgroundColor: Colors.blue,
              child: const Icon(Icons.close, color: Colors.white),
            ),
          ),
        ],
      ),
    );
  }
}

// ignore_for_file: avoid_positional_boolean_parameters

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/audio_shader.dart';
import 'package:flutter_soloud_example/visualizer/bars_fft_widget.dart';
import 'package:flutter_soloud_example/visualizer/bars_wave_widget.dart';
import 'package:flutter_soloud_example/visualizer/bmp_header.dart';
import 'package:flutter_soloud_example/visualizer/paint_texture.dart';

/// enum to tell [Visualizer] to build a texture as:
/// [both1D] frequencies data on the 1st 256px row, wave on the 2nd 256px
/// [fft2D] frequencies data 256x256 px
/// [wave2D] wave data 256x256px
/// [both2D] both frequencies & wave data interleaved 256x512px
enum TextureType {
  both1D,
  fft2D,
  wave2D,
  both2D, // no implemented yet
}

class FftController extends ChangeNotifier {
  FftController({
    this.minFreqRange = 0,
    this.maxFreqRange = 255,
    this.isVisualizerEnabled = true,
    this.isVisualizerForPlayer = true,
  });

  int minFreqRange;
  int maxFreqRange;
  bool isVisualizerEnabled;
  bool isVisualizerForPlayer;

  void changeMinFreq(int minFreq) {
    if (minFreq < 0) return;
    if (minFreq >= maxFreqRange) return;
    minFreqRange = minFreq;
    notifyListeners();
  }

  void changeMaxFreq(int maxFreq) {
    if (maxFreq > 255) return;
    if (maxFreq <= minFreqRange) return;
    maxFreqRange = maxFreq;
    notifyListeners();
  }

  void changeIsVisualizerForPlayer(bool isForPlayer) {
    isVisualizerForPlayer = isForPlayer;
    notifyListeners();
  }

  void changeIsVisualizerEnabled(bool enable) {
    isVisualizerEnabled = enable;
    notifyListeners();
    SoLoud.instance.setVisualizationEnabled(enable);
  }
}

class Visualizer extends StatefulWidget {
  const Visualizer({
    required this.controller,
    required this.shader,
    this.textureType = TextureType.fft2D,
    super.key,
  });

  final FftController controller;
  final ui.FragmentShader shader;
  final TextureType textureType;

  @override
  State<Visualizer> createState() => _VisualizerState();
}

class _VisualizerState extends State<Visualizer>
    with SingleTickerProviderStateMixin {
  late bool isInitialized;
  late bool isCaptureInited;
  late Ticker ticker;
  late Stopwatch sw;
  late Bmp32Header fftImageRow;
  late Bmp32Header fftImageMatrix;
  late int fftSize;
  late int halfFftSize;
  late int fftBitmapRange;
  ffi.Pointer<ffi.Pointer<ffi.Float>> playerData = ffi.nullptr;
  ffi.Pointer<ffi.Pointer<ffi.Float>> captureData = ffi.nullptr;
  late Future<ui.Image?> Function() buildImageCallback;
  late int Function(int row, int col) textureTypeCallback;
  int nFrames = 0;

  @override
  void initState() {
    super.initState();

    isInitialized = SoLoud.instance.isInitialized;
    isCaptureInited = SoLoudCapture.instance.isCaptureInited;
    SoLoud.instance.audioEvent.stream.listen(
      (event) {
        isInitialized = SoLoud.instance.isInitialized;
        isCaptureInited = SoLoudCapture.instance.isCaptureInited;
      },
    );

    /// these constants must not be touched since SoLoud
    /// gives back a size of 256 values
    fftSize = 512;
    halfFftSize = fftSize >> 1;

    playerData = calloc();
    captureData = calloc();

    ticker = createTicker(_tick);
    sw = Stopwatch();
    sw.start();
    setupBitmapSize();
    ticker.start();

    widget.controller.addListener(() {
      ticker.stop();
      setupBitmapSize();
      ticker.start();
      sw.reset();
      nFrames = 0;
    });
  }

  @override
  void dispose() {
    ticker.stop();
    sw.stop();
    calloc.free(playerData);
    playerData = ffi.nullptr;
    calloc.free(captureData);
    captureData = ffi.nullptr;
    super.dispose();
  }

  void _tick(Duration elapsed) {
    nFrames++;
    if (mounted) {
      setState(() {});
    }
  }

  void setupBitmapSize() {
    fftBitmapRange =
        widget.controller.maxFreqRange - widget.controller.minFreqRange;
    fftImageRow = Bmp32Header.setHeader(fftBitmapRange, 2);
    fftImageMatrix = Bmp32Header.setHeader(fftBitmapRange, 256);

    switch (widget.textureType) {
      case TextureType.both1D:
        {
          buildImageCallback = buildImageFromLatestSamplesRow;
          break;
        }
      case TextureType.fft2D:
        {
          buildImageCallback = buildImageFromAllSamplesMatrix;
          textureTypeCallback = getFFTDataCallback;
          break;
        }
      case TextureType.wave2D:
        {
          buildImageCallback = buildImageFromAllSamplesMatrix;
          textureTypeCallback = getWaveDataCallback;
          break;
        }
      // TODO(me): implement this
      case TextureType.both2D:
        {
          buildImageCallback = buildImageFromAllSamplesMatrix;
          textureTypeCallback = getWaveDataCallback;
          break;
        }
    }
  }

  @override
  Widget build(BuildContext context) {
    return FutureBuilder<ui.Image?>(
      future: buildImageCallback(),
      builder: (context, dataTexture) {
        final fps = nFrames.toDouble() / (sw.elapsedMilliseconds / 1000.0);
        if (!dataTexture.hasData || dataTexture.data == null) {
          return Placeholder(
            color: Colors.yellow,
            fallbackWidth: 100,
            fallbackHeight: 100,
            strokeWidth: 0.5,
            child: Text("can't get audio samples\n"
                'FPS: ${fps.toStringAsFixed(1)}'),
          );
        }

        final nFft =
            widget.controller.maxFreqRange - widget.controller.minFreqRange;

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
                  child: AudioShader(
                    width: constraints.maxWidth,
                    height: constraints.maxWidth / 2.4,
                    image: dataTexture.data!,
                    shader: widget.shader,
                    iTime: sw.elapsedMilliseconds / 1000.0,
                  ),
                ),

                Row(
                  children: [
                    Column(
                      children: [
                        Text(
                          '$nFft FFT data',
                          style: const TextStyle(fontWeight: FontWeight.bold),
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
                            audioData: widget.controller.isVisualizerForPlayer
                                ? playerData.value
                                : captureData.value,
                            minFreq: widget.controller.minFreqRange,
                            maxFreq: widget.controller.maxFreqRange,
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
                            audioData: widget.controller.isVisualizerForPlayer
                                ? playerData.value
                                : captureData.value,
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

  /// build an image to be passed to the shader.
  /// The image is a matrix of 256x2 RGBA pixels representing:
  /// in the 1st row the frequencies data
  /// in the 2nd row the wave data
  Future<ui.Image?> buildImageFromLatestSamplesRow() async {
    if (!widget.controller.isVisualizerEnabled) {
      return null;
    }

    /// get audio data from player or capture device
    if (widget.controller.isVisualizerForPlayer && isInitialized) {
      try {
        SoLoud.instance.getAudioTexture2D(playerData);
      } catch (e) {
        return null;
      }
    } else if (!widget.controller.isVisualizerForPlayer && isCaptureInited) {
      final ret = SoLoudCapture.instance.getCaptureAudioTexture2D(captureData);
      if (ret != CaptureErrors.captureNoError) {
        return null;
      }
    } else {
      return null;
    }

    if (!mounted) {
      return null;
    }

    final completer = Completer<ui.Image>();
    final bytes = Uint8List(fftBitmapRange * 2 * 4);
    // Fill the texture bitmap
    var col = 0;
    for (var i = widget.controller.minFreqRange;
        i < widget.controller.maxFreqRange;
        ++i, ++col) {
      // fill 1st bitmap row with magnitude
      bytes[col * 4 + 0] = getFFTDataCallback(0, i);
      bytes[col * 4 + 1] = 0;
      bytes[col * 4 + 2] = 0;
      bytes[col * 4 + 3] = 255;
      // fill 2nd bitmap row with amplitude
      bytes[(fftBitmapRange + col) * 4 + 0] = getWaveDataCallback(0, i);
      bytes[(fftBitmapRange + col) * 4 + 1] = 0;
      bytes[(fftBitmapRange + col) * 4 + 2] = 0;
      bytes[(fftBitmapRange + col) * 4 + 3] = 255;
    }

    final img = fftImageRow.storeBitmap(bytes);
    ui.decodeImageFromList(img, completer.complete);

    return completer.future;
  }

  /// build an image to be passed to the shader.
  /// The image is a matrix of 256x256 RGBA pixels representing
  /// rows of wave data or frequencies data.
  /// Passing [getWaveDataCallback] as parameter, it will return wave data
  /// Passing [getFFTDataCallback] as parameter, it will return FFT data
  Future<ui.Image?> buildImageFromAllSamplesMatrix() async {
    if (!widget.controller.isVisualizerEnabled) {
      return null;
    }

    /// get audio data from player or capture device
    if (widget.controller.isVisualizerForPlayer && isInitialized) {
      try {
        SoLoud.instance.getAudioTexture2D(playerData);
      } catch (e) {
        return null;
      }
    } else if (!widget.controller.isVisualizerForPlayer && isCaptureInited) {
      final ret = SoLoudCapture.instance.getCaptureAudioTexture2D(captureData);
      if (ret != CaptureErrors.captureNoError) {
        return null;
      }
    } else {
      return null;
    }

    if (!mounted) {
      return null;
    }

    /// IMPORTANT: if [mounted] is not checked here, could happens that
    /// dispose() is called before this is called but it is called!
    /// Since in dispose the [audioData] is freed, there will be a crash!
    /// I do not understand why this happens because the FutureBuilder
    /// seems has not finished before dispose()!?
    /// My psychoanalyst told me to forget it and my mom to study more
    if (!mounted) {
      return null;
    }
    final completer = Completer<ui.Image>();
    final bytes = Uint8List(fftBitmapRange * 256 * 4);

    // Fill the texture bitmap with wave data
    for (var y = 0; y < 256; ++y) {
      var col = 0;
      for (var x = widget.controller.minFreqRange;
          x < widget.controller.maxFreqRange;
          ++x, ++col) {
        bytes[y * fftBitmapRange * 4 + col * 4 + 0] = textureTypeCallback(y, x);
        bytes[y * fftBitmapRange * 4 + col * 4 + 1] = 0;
        bytes[y * fftBitmapRange * 4 + col * 4 + 2] = 0;
        bytes[y * fftBitmapRange * 4 + col * 4 + 3] = 255;
      }
    }

    final img = fftImageMatrix.storeBitmap(bytes);
    ui.decodeImageFromList(img, completer.complete);

    return completer.future;
  }

  int getFFTDataCallback(int row, int col) {
    if (widget.controller.isVisualizerForPlayer) {
      return (playerData.value[row * fftSize + col] * 255.0).toInt();
    } else {
      return (captureData.value[row * fftSize + col] * 255.0).toInt();
    }
  }

  int getWaveDataCallback(int row, int col) {
    if (widget.controller.isVisualizerForPlayer) {
      return (((playerData.value[row * fftSize + halfFftSize + col] + 1.0) /
                  2.0) *
              128)
          .toInt();
    } else {
      return (((captureData.value[row * fftSize + halfFftSize + col] + 1.0) /
                  2.0) *
              128)
          .toInt();
    }
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

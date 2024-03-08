import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/visualizer.dart';
import 'package:logging/logging.dart';
import 'package:path_provider/path_provider.dart';
import 'package:star_menu/star_menu.dart';

class PageVisualizer extends StatefulWidget {
  const PageVisualizer({super.key});

  @override
  State<PageVisualizer> createState() => _PageVisualizerState();
}

class _PageVisualizerState extends State<PageVisualizer> {
  static final Logger _log = Logger('_PageVisualizerState');

  String shader = 'assets/shaders/test9.frag';
  final regExp = RegExp('_(s[w|i].*)_-');
  final List<String> audioChecks = [
    'assets/audio/audiocheck.net_sweep_20Hz_20000Hz_-3dBFS_4s_logarithmic.wav',
    'assets/audio/audiocheck.net_sin_8000Hz_48000_-3dBFS_3s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_16Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_31.5Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_63Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_125Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_250Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_500Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_1000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_2000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_4000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_8000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_16000Hz_-3dBFS_2s.wav',
    'assets/audio/12Bands/audiocheck.net_sin_20000Hz_-3dBFS_2s.wav',
  ];
  final ValueNotifier<TextureType> textureType =
      ValueNotifier(TextureType.fft2D);
  final ValueNotifier<double> fftSmoothing = ValueNotifier(0.8);
  final ValueNotifier<bool> isVisualizerForPlayer = ValueNotifier(false);
  final ValueNotifier<bool> isVisualizerEnabled = ValueNotifier(true);
  final ValueNotifier<RangeValues> fftImageRange =
      ValueNotifier(const RangeValues(0, 255));
  final ValueNotifier<int> maxFftImageRange = ValueNotifier(255);
  final ValueNotifier<double> soundLength = ValueNotifier(0);
  final ValueNotifier<double> soundPosition = ValueNotifier(0);
  Timer? timer;
  SoundProps? currentSound;
  FftController visualizerController = FftController();

  @override
  void dispose() {
    SoLoud().dispose();
    SoLoud().stopCapture();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SingleChildScrollView(
        child: Column(
          children: [
            /// audio & frags popup menu
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                /// audio samples popup menu
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
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        playAsset('assets/audio/8_bit_mentality.mp3');
                      },
                      label: const Text('8 bit mentality'),
                    ),
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        playAsset('assets/audio/Tropical Beeper.mp3');
                      },
                      label: const Text('Tropical Beeper'),
                    ),
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        playAsset('assets/audio/X trackTure.mp3');
                      },
                      label: const Text('X trackTure'),
                    ),
                    for (int i = 0; i < audioChecks.length; i++)
                      ActionChip(
                        backgroundColor: Colors.blue,
                        onPressed: () {
                          playAsset(audioChecks[i]);
                        },
                        label: Text(
                          regExp.firstMatch(audioChecks[i])?.group(1) ?? '',
                        ),
                      ),
                  ],
                  child: const Chip(
                    label: Text('samples'),
                    backgroundColor: Colors.blue,
                    avatar: Icon(Icons.arrow_drop_down),
                  ),
                ),
                const SizedBox(width: 10),

                /// frags popup menu
                StarMenu(
                  params: StarMenuParameters(
                    shape: MenuShape.linear,
                    boundaryBackground: BoundaryBackground(
                      color: Colors.white.withOpacity(0.1),
                      blurSigmaX: 6,
                      blurSigmaY: 6,
                    ),
                    linearShapeParams: const LinearShapeParams(
                      angle: -90,
                      space: 10,
                      alignment: LinearAlignment.left,
                    ),
                  ),
                  onItemTapped: (index, controller) {
                    controller.closeMenu!();
                  },
                  items: [
                    for (int i = 1; i <= 9; ++i)
                      ActionChip(
                        backgroundColor: Colors.blue,
                        onPressed: () {
                          setState(() {
                            shader = 'assets/shaders/test$i.frag';
                          });
                        },
                        label: Text(i.toString()),
                      ),
                  ],
                  child: const Chip(
                    label: Text('shaders'),
                    backgroundColor: Colors.blue,
                    avatar: Icon(Icons.arrow_drop_down),
                  ),
                ),
                const SizedBox(width: 10),

                /// texture type
                StarMenu(
                  params: StarMenuParameters(
                    shape: MenuShape.linear,
                    boundaryBackground: BoundaryBackground(
                      color: Colors.white.withOpacity(0.1),
                      blurSigmaX: 6,
                      blurSigmaY: 6,
                    ),
                    linearShapeParams: const LinearShapeParams(
                      angle: -90,
                      space: 10,
                      alignment: LinearAlignment.left,
                    ),
                  ),
                  onItemTapped: (index, controller) {
                    controller.closeMenu!();
                  },
                  items: [
                    /// frequencies on 1st 256 px row
                    /// wave on 2nd 256 px row
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        textureType.value = TextureType.both1D;
                      },
                      label: const Text('both 1D'),
                    ),

                    /// frequencies (FFT)
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        textureType.value = TextureType.fft2D;
                      },
                      label: const Text('frequencies'),
                    ),

                    /// wave data (amplitudes)
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        textureType.value = TextureType.wave2D;
                      },
                      label: const Text('wave data'),
                    ),

                    /// both fft and wave
                    /// not implemented yet
                    // ActionChip(
                    //   backgroundColor: Colors.blue,
                    //   onPressed: () {
                    //     textureType.value = TextureType.both2D;
                    //   },
                    //   label: const Text('both'),
                    // ),
                  ],
                  child: const Chip(
                    label: Text('texture'),
                    backgroundColor: Colors.blue,
                    avatar: Icon(Icons.arrow_drop_down),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 8),

            /// Text to Speech & pick audio file
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                /// text 2 speech
                ElevatedButton(
                  onPressed: () {
                    SoLoud().speechText('Hello Flutter. Text to speech!!');
                  },
                  child: const Text('T2S'),
                ),
                const SizedBox(width: 6),

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
              ],
            ),

            /// Seek slider
            ValueListenableBuilder<double>(
              valueListenable: soundLength,
              builder: (_, length, __) {
                return ValueListenableBuilder<double>(
                  valueListenable: soundPosition,
                  builder: (_, position, __) {
                    if (position >= length) {
                      position = 0;
                      if (length == 0) length = 1;
                    }

                    return Row(
                      children: [
                        Text(position.toInt().toString()),
                        Expanded(
                          child: Slider.adaptive(
                            value: position,
                            max: length < position ? position : length,
                            onChanged: (value) {
                              if (currentSound == null) return;
                              stopTimer();
                              SoLoud().seek(currentSound!.handle.last, value);
                              soundPosition.value = value;
                              startTimer();
                            },
                          ),
                        ),
                        Text(length.toInt().toString()),
                      ],
                    );
                  },
                );
              },
            ),

            /// fft range slider values to put into the texture
            ValueListenableBuilder<RangeValues>(
              valueListenable: fftImageRange,
              builder: (_, fftRange, __) {
                return Row(
                  children: [
                    Text('FFT range ${fftRange.start.toInt()}'),
                    Expanded(
                      child: RangeSlider(
                        max: 255,
                        divisions: 256,
                        values: fftRange,
                        onChanged: (values) {
                          fftImageRange.value = values;
                          visualizerController
                            ..changeMinFreq(values.start.toInt())
                            ..changeMaxFreq(values.end.toInt());
                        },
                      ),
                    ),
                    Text('${fftRange.end.toInt()}'),
                  ],
                );
              },
            ),

            /// fft smoothing slider
            ValueListenableBuilder<double>(
              valueListenable: fftSmoothing,
              builder: (_, smoothing, __) {
                return Row(
                  children: [
                    Text('FFT smooth: ${smoothing.toStringAsFixed(2)}'),
                    Expanded(
                      child: Slider.adaptive(
                        value: smoothing,
                        onChanged: (smooth) {
                          if (isVisualizerForPlayer.value) {
                            SoLoud().setFftSmoothing(smooth);
                          } else {
                            SoLoud().setCaptureFftSmoothing(smooth);
                          }
                          fftSmoothing.value = smooth;
                        },
                      ),
                    ),
                  ],
                );
              },
            ),

            /// switch for getting data from player or from mic
            ValueListenableBuilder<bool>(
              valueListenable: isVisualizerForPlayer,
              builder: (_, forPlayer, __) {
                return Row(
                  children: [
                    Checkbox(
                      value: !forPlayer,
                      onChanged: (value) {
                        isVisualizerForPlayer.value = !value!;
                        visualizerController
                            .changeIsVisualizerForPlayer(!value);
                      },
                    ),
                    const Text('show capture data'),
                    Checkbox(
                      value: forPlayer,
                      onChanged: (value) {
                        isVisualizerForPlayer.value = value!;
                        visualizerController.changeIsVisualizerForPlayer(value);
                      },
                    ),
                    const Text('show player data'),
                  ],
                );
              },
            ),

            /// switch to enable / disable retrieving audio data
            ValueListenableBuilder<bool>(
              valueListenable: isVisualizerEnabled,
              builder: (_, isEnabled, __) {
                return Row(
                  children: [
                    Switch(
                      materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
                      value: isEnabled,
                      onChanged: (value) {
                        isVisualizerEnabled.value = value;
                        visualizerController.changeIsVisualizerEnabled(value);
                      },
                    ),
                    const Text('FFT data'),
                  ],
                );
              },
            ),

            /// VISUALIZER
            FutureBuilder<ui.FragmentShader?>(
              future: loadShader(),
              builder: (context, snapshot) {
                if (snapshot.hasData) {
                  return ValueListenableBuilder<TextureType>(
                    valueListenable: textureType,
                    builder: (_, type, __) {
                      // return SizedBox.shrink();
                      return Visualizer(
                        key: UniqueKey(),
                        controller: visualizerController,
                        shader: snapshot.data!,
                        textureType: type,
                      );
                    },
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
          ],
        ),
      ),
    );
  }

  /// load asynchronously the fragment shader
  Future<ui.FragmentShader?> loadShader() async {
    try {
      final program = await ui.FragmentProgram.fromAsset(shader);
      return program.fragmentShader();
    } catch (e) {
      _log.severe('error compiling the shader', e);
    }
    return null;
  }

  /// play file
  Future<void> play(String file) async {
    if (currentSound != null) {
      if (await SoLoud().disposeSound(currentSound!) != PlayerErrors.noError) {
        return;
      }
      stopTimer();
    }

    /// load the file
    final loadRet = await SoLoud().loadFile(file);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = loadRet.sound;

    /// play it
    final playRet = await SoLoud().play(currentSound!);
    if (loadRet.error != PlayerErrors.noError) return;
    currentSound = playRet.sound;

    /// get its length and notify it
    soundLength.value = SoLoud().getLength(currentSound!).length;

    /// Stop the timer and dispose the sound when the sound ends
    currentSound!.soundEvents.stream.listen(
      (event) {
        stopTimer();
        // TODO(me): put this elsewhere?
        event.sound.soundEvents.close();

        /// It's needed to call dispose when it end else it will
        /// not be cleared
        SoLoud().disposeSound(currentSound!);
        currentSound = null;
      },
    );
    startTimer();
  }

  /// plays an assets file
  Future<void> playAsset(String assetsFile) async {
    final audioFile = await getAssetFile(assetsFile);
    return play(audioFile.path);
  }

  /// get the assets file and copy it to the temp dir
  Future<File> getAssetFile(String assetsFile) async {
    final tempDir = await getTemporaryDirectory();
    final tempPath = tempDir.path;
    final filePath = '$tempPath/$assetsFile';
    final file = File(filePath);
    if (file.existsSync()) {
      return file;
    } else {
      final byteData = await rootBundle.load(assetsFile);
      final buffer = byteData.buffer;
      await file.create(recursive: true);
      return file.writeAsBytes(
        buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes),
      );
    }
  }

  /// start timer to update the audio position slider
  void startTimer() {
    timer?.cancel();
    timer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (currentSound != null) {
        soundPosition.value =
            SoLoud().getPosition(currentSound!.handle.last).position;
      }
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/visualizer/visualizer.dart';
import 'package:logging/logging.dart';
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
  late final ValueNotifier<GetSamplesKind> samplesKind;
  final ValueNotifier<double> fftSmoothing = ValueNotifier(0.8);
  final ValueNotifier<bool> isVisualizerEnabled = ValueNotifier(true);
  late ValueNotifier<RangeValues> fftImageRange;
  final ValueNotifier<double> soundLength = ValueNotifier(0);
  final ValueNotifier<double> soundPosition = ValueNotifier(0);
  Timer? timer;
  AudioSource? currentSound;
  late final VisualizerController visualizerController;

  @override
  void initState() {
    super.initState();
    samplesKind = ValueNotifier(GetSamplesKind.linear);
    visualizerController = VisualizerController(samplesKind: samplesKind.value);
    fftImageRange = ValueNotifier(
      RangeValues(
        visualizerController.minRange.toDouble(),
        visualizerController.maxRange.toDouble(),
      ),
    );
  }

  @override
  void dispose() {
    visualizerController.audioData.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SingleChildScrollView(
        child: Column(
          children: [
            /// audio, shader and texture kind popup menu
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
                      space: defaultTargetPlatform == TargetPlatform.android ||
                              defaultTargetPlatform == TargetPlatform.iOS
                          ? -10
                          : 10,
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
                        playAsset('assets/audio/TropicalBeeper.mp3');
                      },
                      label: const Text('Tropical Beeper'),
                    ),
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        playAsset('assets/audio/XtrackTure.mp3');
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

                /// shaders popup menu
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

                /// texture kind
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
                    /// wave data (amplitudes)
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        samplesKind.value = GetSamplesKind.wave;
                        visualizerController
                            .changeSamplesKind(GetSamplesKind.wave);
                        fftImageRange.value = const RangeValues(0, 255);
                      },
                      label: const Text('wave data'),
                    ),

                    /// frequencies on 1st 256 px row
                    /// wave on 2nd 256 px row
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        samplesKind.value = GetSamplesKind.linear;
                        visualizerController
                            .changeSamplesKind(GetSamplesKind.linear);
                        fftImageRange.value = const RangeValues(0, 255);
                      },
                      label: const Text('linear'),
                    ),

                    /// both fft and wave
                    ActionChip(
                      backgroundColor: Colors.blue,
                      onPressed: () {
                        samplesKind.value = GetSamplesKind.texture;
                        visualizerController
                            .changeSamplesKind(GetSamplesKind.texture);
                        fftImageRange.value = const RangeValues(0, 511);
                      },
                      label: const Text('texture'),
                    ),
                  ],
                  child: ValueListenableBuilder<GetSamplesKind>(
                    valueListenable: samplesKind,
                    builder: (_, type, __) {
                      return Chip(
                        label: Text(type.name),
                        backgroundColor: Colors.blue,
                        avatar: const Icon(Icons.arrow_drop_down),
                      );
                    },
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
                    SoLoud.instance
                        .speechText('Hello Flutter. Text to speech!!');
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
                      final AudioSource audioFile;
                      if (kIsWeb) {
                        audioFile = await SoLoud.instance
                            .loadMem(paths.first.name, paths.first.bytes!);
                      } else {
                        audioFile =
                            await SoLoud.instance.loadFile(paths.first.path!);
                      }
                      unawaited(play(audioFile));
                    }
                  },
                  child: const Text('pick audio'),
                ),
              ],
            ),

            /// Seek slider.
            /// Not used on web platforms because [LoadMode.disk]
            /// is used with `loadMem()`. Otherwise the seek problem will
            /// be noticeable while seeking. See [SoLoud.seek] note.
            if (!kIsWeb)
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
                                final position = Duration(
                                  milliseconds:
                                      (value * Duration.millisecondsPerSecond)
                                          .round(),
                                );
                                SoLoud.instance
                                    .seek(currentSound!.handles.last, position);
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
                        max: visualizerController.maxRangeLimit.toDouble() + 1,
                        values: fftRange,
                        onChanged: (values) {
                          fftImageRange.value = values;
                          visualizerController
                            ..changeMin(values.start.toInt())
                            ..changeMax(values.end.toInt());
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
                          SoLoud.instance.setFftSmoothing(smooth);
                          fftSmoothing.value = smooth;
                        },
                      ),
                    ),
                  ],
                );
              },
            ),

            /// VISUALIZER
            Visualizer(
              // key: UniqueKey(),
              controller: visualizerController,
              shader: shader,
            ),
          ],
        ),
      ),
    );
  }

  /// play file
  Future<void> play(AudioSource source) async {
    if (currentSound != null) {
      try {
        await SoLoud.instance.disposeSource(currentSound!);
      } catch (e) {
        _log.severe('error disposing the current sound', e);
        return;
      }
      stopTimer();
    }
    currentSound = source;

    /// play it
    await SoLoud.instance.play(currentSound!);

    /// get its length and notify it
    soundLength.value =
        SoLoud.instance.getLength(currentSound!).inMilliseconds /
            Duration.millisecondsPerSecond;

    /// Stop the timer and dispose the sound when the sound ends
    currentSound!.soundEvents.listen(
      (event) {
        stopTimer();

        /// It's needed to call dispose when it ends else it will
        /// not be cleared
        if (currentSound != null) {
          SoLoud.instance.disposeSource(currentSound!);
          currentSound = null;
        }
      },
    );
    startTimer();
  }

  /// plays an assets file
  Future<void> playAsset(String assetsFile) async {
    // final audioFile = await getAssetFile(assetsFile);
    final audioFile = await SoLoud.instance.loadAsset(
      assetsFile,
      mode: kIsWeb ? LoadMode.disk : LoadMode.memory,
    );
    return play(audioFile);
  }

  /// start timer to update the audio position slider
  void startTimer() {
    timer?.cancel();
    timer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (currentSound != null) {
        soundPosition.value = SoLoud.instance
                .getPosition(currentSound!.handles.last)
                .inMilliseconds /
            Duration.millisecondsPerSecond;
      }
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

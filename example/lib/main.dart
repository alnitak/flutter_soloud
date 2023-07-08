import 'dart:async';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud_bindings_ffi.dart';
import 'package:flutter_soloud/soloud_controller.dart';
import 'package:path_provider/path_provider.dart';
import 'package:star_menu/star_menu.dart';

import 'visualizer.dart';

SoLoudController soLoudController = SoLoudController();

void main() {
  soLoudController.initialize();
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        dragDevices: {
          PointerDeviceKind.mouse,
          PointerDeviceKind.touch,
          PointerDeviceKind.stylus,
          PointerDeviceKind.unknown,
        },
      ),
      home: const HomePage(),
    );
  }
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});

  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
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
  final ValueNotifier<RangeValues> fftImageRange =
      ValueNotifier(const RangeValues(0, 255));
  final ValueNotifier<int> maxFftImageRange = ValueNotifier(255);
  final ValueNotifier<double> soundLength = ValueNotifier(0);
  final ValueNotifier<double> soundPosition = ValueNotifier(0);
  Timer? timer;

  @override
  void initState() {
    super.initState();
    startTimer();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.only(top: 50, right: 8, left: 8),
        child: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.start,
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
                        onPressed: () async {
                          playAsset('assets/audio/Tropical Beeper.mp3');
                        },
                        label: const Text('Tropical Beeper'),
                      ),
                      ActionChip(
                        backgroundColor: Colors.blue,
                        onPressed: () async {
                          playAsset('assets/audio/X trackTure.mp3');
                        },
                        label: const Text('X trackTure'),
                      ),
                      for (int i = 0; i < audioChecks.length; i++)
                        ActionChip(
                          backgroundColor: Colors.blue,
                          onPressed: () async {
                            soLoudController.soLoudFFI.playFile(audioChecks[i]);
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
                      ActionChip(
                        backgroundColor: Colors.blue,
                        onPressed: () {
                          textureType.value = TextureType.both2D;
                        },
                        label: const Text('both'),
                      ),
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
                  /// init audio engine
                  ElevatedButton(
                    onPressed: () async {
                      if (soLoudController.soLoudFFI.initEngine() ==
                          PlayerErrors.noError) {
                        soLoudController.soLoudFFI
                            .setVisualizationEnabled(true);
                      } else {
                        ScaffoldMessenger.of(context).showSnackBar(
                          const SnackBar(
                            content: Text('Audio initialization error!!'),
                          ),
                        );
                      }
                    },
                    child: const Text('init'),
                  ),
                  const SizedBox(width: 6),

                  /// dispose audio engine
                  ElevatedButton(
                    onPressed: () async {
                      soLoudController.soLoudFFI.dispose();
                    },
                    child: const Text('dispose'),
                  ),
                  const SizedBox(width: 20),

                  /// text 2 speech
                  ElevatedButton(
                    onPressed: () async {
                      soLoudController.soLoudFFI
                          .speechText('Hello Flutter. Text to speech!!');
                    },
                    child: const Text('T2S'),
                  ),
                  const SizedBox(width: 6),

                  /// pick audio file
                  ElevatedButton(
                    onPressed: () async {
                      final paths = (await FilePicker.platform.pickFiles(
                        type: FileType.audio,
                        onFileLoading: print,
                        dialogTitle: 'Pick audio file',
                      ))
                          ?.files;
                      if (paths != null) {
                        play(paths.first.path!);
                      }
                    },
                    child: const Text('pick audio'),
                  ),
                ],
              ),
              const SizedBox(height: 16),

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
                                stopTimer();
                                soLoudController.soLoudFFI.seek(value);
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

              /// fft range to put into the texture
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
                          onChanged: (value) {
                            soLoudController.soLoudFFI.setFftSmoothing(value);
                            fftSmoothing.value = value;
                          },
                        ),
                      ),
                    ],
                  );
                },
              ),
              const SizedBox(height: 8),

              /// VISUALIZER
              FutureBuilder<ui.FragmentShader?>(
                future: loadShader(),
                builder: (context, snapshot) {
                  if (snapshot.hasData) {
                    return ValueListenableBuilder<RangeValues>(
                      valueListenable: fftImageRange,
                      builder: (_, fftRange, __) {
                        return ValueListenableBuilder<TextureType>(
                          valueListenable: textureType,
                          builder: (_, type, __) {
                            // return SizedBox.shrink();
                            return Visualizer(
                              key: UniqueKey(),
                              shader: snapshot.data!,
                              textureType: type,
                              minImageFreqRange: fftRange.start.toInt(),
                              maxImageFreqRange: fftRange.end.toInt(),
                            );
                          },
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
      ),
    );
  }

  /// load asynchronously the fragment shader
  Future<ui.FragmentShader?> loadShader() async {
    try {
      final program = await ui.FragmentProgram.fromAsset(shader);
      return program.fragmentShader();
    } catch (e) {
      debugPrint('error compiling the shader: $e');
    }
    return null;
  }

  /// play file
  void play(String file) {
    soLoudController.soLoudFFI.playFile(file);
    soundLength.value = soLoudController.soLoudFFI.getLength();
  }

  /// plays an assets file
  Future<void> playAsset(String assetsFile) async {
    File audioFile = await getAssetFile(assetsFile);
    soLoudController.soLoudFFI.playFile(audioFile.path);
    soundLength.value = soLoudController.soLoudFFI.getLength();
  }

  /// get the assets file and copy it to the temp dir
  Future<File> getAssetFile(String assetsFile) async {
    Directory tempDir = await getTemporaryDirectory();
    String tempPath = tempDir.path;
    var filePath = '$tempPath/$assetsFile';
    var file = File(filePath);
    if (file.existsSync()) {
      return file;
    } else {
      final byteData = await rootBundle.load(assetsFile);
      final buffer = byteData.buffer;
      await file.create(recursive: true);
      return file.writeAsBytes(
          buffer.asUint8List(byteData.offsetInBytes, byteData.lengthInBytes));
    }
  }

  /// start timer to update the audio position slider
  void startTimer() {
    timer = Timer.periodic(const Duration(seconds: 1), (_) {
      soundPosition.value = soLoudController.soLoudFFI.getPosition();
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

// ignore_for_file: public_member_api_docs, sort_constructors_first
import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:logging/logging.dart';
import 'package:path_provider/path_provider.dart';

class PageMultiTrack extends StatefulWidget {
  const PageMultiTrack({super.key});

  @override
  State<PageMultiTrack> createState() => _PageMultiTrackState();
}

class _PageMultiTrackState extends State<PageMultiTrack> {
  static final Logger _log = Logger('_PageMultiTrackState');

  final _looping = ValueNotifier<bool>(false);
  final _loopingStartAt = ValueNotifier<double>(0);
  final _playSoundController = PlaySoundController();
  bool canBuild = false;

  @override
  void initState() {
    super.initState();

    /// Initialize the player
    SoLoud.instance.initialize().then((value) {
      if (value == PlayerErrors.multipleInitialization) {
        SoLoud.instance.disposeAllSound();
      }
      if (value == PlayerErrors.noError ||
          value == PlayerErrors.multipleInitialization) {
        _log.info('player started');
        // SoLoud.instance.setVisualizationEnabled(false);
        SoLoud.instance.setGlobalVolume(1);
        if (context.mounted) {
          setState(() {
            canBuild = true;
          });
        }
      } else {
        _log.severe('player starting error: $value');
        return;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    if (!SoLoud.instance.isInitialized) return const SizedBox.shrink();

    return Scaffold(
      body: Padding(
        padding: const EdgeInsets.only(top: 50, right: 8, left: 8),
        child: Column(
          children: [
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                PlaySoundWidget(
                  assetsAudio: 'assets/audio/8_bit_mentality.mp3',
                  text: '8 bit mentality',
                  controller: _playSoundController,
                ),
                const SizedBox(height: 32),
                ValueListenableBuilder(
                  valueListenable: _looping,
                  builder: (_, looping, __) {
                    return ValueListenableBuilder(
                      valueListenable: _loopingStartAt,
                      builder: (_, loopingStartAt, __) {
                        return Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                const Text('looping'),
                                const SizedBox(width: 16),
                                Checkbox(
                                  value: looping,
                                  onChanged: (value) {
                                    _looping.value = !looping;
                                    _playSoundController.setLooping(value!);
                                  },
                                ),
                                Expanded(
                                  child: Slider(
                                    value: loopingStartAt,
                                    max: 3.8, // the length of explosion.mp3
                                    onChanged: (value) {
                                      _loopingStartAt.value = value;
                                      _playSoundController
                                          .setLoopStartAT(value);
                                    },
                                  ),
                                ),
                              ],
                            ),
                            PlaySoundWidget(
                              assetsAudio: 'assets/audio/explosion.mp3',
                              text: 'game explosion',
                              controller: _playSoundController,
                            ),
                          ],
                        );
                      },
                    );
                  },
                ),
              ],
            ),
            const SizedBox(height: 12),
          ],
        ),
      ),
    );
  }
}

/// Controller to manage loop and loopStartAt parameters of all sounds.
class PlaySoundController {
  void Function(bool looping)? _looping;
  void Function(double loopStartAt)? _loopingStartAt;

  void _setController(
    void Function(bool looping)? looping,
    void Function(double loopStartAt)? loopingStartAt,
  ) {
    _looping = looping;
    _loopingStartAt = loopingStartAt;
  }

  void setLooping(bool looping) => _looping?.call(looping);
  void setLoopStartAT(double loopStartAt) => _loopingStartAt?.call(loopStartAt);
}

class PlaySoundWidget extends StatefulWidget {
  const PlaySoundWidget({
    required this.assetsAudio,
    required this.text,
    required this.controller,
    super.key,
  });

  final String assetsAudio;
  final String text;
  final PlaySoundController controller;

  @override
  State<PlaySoundWidget> createState() => _PlaySoundWidgetState();
}

class _PlaySoundWidgetState extends State<PlaySoundWidget> {
  static final Logger _log = Logger('_PlaySoundWidgetState');

  late double soundLength;
  final Map<SoundHandle, ValueNotifier<bool>> isPaused = {};
  final Map<SoundHandle, ValueNotifier<double>> soundPosition = {};
  StreamSubscription<StreamSoundEvent>? _subscription;
  SoundProps? sound;
  bool _looping = false;
  double _loopingStartAt = 0;

  @override
  void initState() {
    super.initState();
    widget.controller._setController(_setLooping, _setLoopStartAt);
  }

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
  }

  void _setLooping(bool looping) {
    _looping = looping;
    if (sound != null) {
      for (final element in sound!.handles) {
        SoLoud.instance.setLooping(element, looping);
      }
    }
  }

  void _setLoopStartAt(double loopingStartAt) {
    _loopingStartAt = loopingStartAt;
    if (sound != null) {
      for (final element in sound!.handles) {
        SoLoud.instance.setLoopPoint(element, loopingStartAt);
      }
    }
  }

  Future<bool> loadAsset() async {
    final path = (await getAssetFile(widget.assetsAudio)).path;
    final loadRet = await SoLoud.instance.loadFile(path);

    if (loadRet.error == PlayerErrors.noError) {
      soundLength = SoLoud.instance.getLength(loadRet.sound!).length;
      sound = loadRet.sound;

      /// Listen to this sound events
      _subscription = sound!.soundEvents.stream.listen(
        (event) {
          _log.fine('Received StreamSoundEvent ${event.event}');

          /// if handle has been stoppend of has finished to play
          if (event.event == SoundEvent.handleIsNoMoreValid) {
            isPaused.remove(event.handle);
            soundPosition.remove(event.handle);
          }

          /// if the sound has been disposed
          if (event.event == SoundEvent.soundDisposed) {
            isPaused.clear();
            soundPosition.clear();
            _subscription?.cancel();
            sound = null;
          }

          if (mounted) setState(() {});
        },
      );
    } else {
      _log.severe('Load sound asset failed: ${loadRet.error}');
      return false;
    }
    return true;
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        ElevatedButton(
          onPressed: () async {
            await playAnotherInstance();
            if (mounted) setState(() {});
          },
          child: Text('load\n${widget.text}'),
        ),
        if (sound != null)
          for (int i = 0; i < sound!.handles.length; ++i)
            PlayingRow(
              handle: sound!.handles.elementAt(i),
              soundLength: soundLength,
              onStopped: () {
                if (mounted) setState(() {});
              },
            ),
      ],
    );
  }

  /// plays an assets file
  ///
  /// the 1st time call, the sound must be loaded.
  /// Other calls, the sound is already in memory and no more
  /// lag before play will happens
  Future<void> playAnotherInstance() async {
    if (sound == null) {
      if (!(await loadAsset())) return;
    }

    final newHandle = await SoLoud.instance.play(
      sound!,
      looping: _looping,
      loopingStartAt: _loopingStartAt,
    );
    if (newHandle.error != PlayerErrors.noError) return;

    isPaused[newHandle.newHandle] = ValueNotifier(false);
    soundPosition[newHandle.newHandle] = ValueNotifier(0);
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
}

/// row widget containing play/pause and time slider
class PlayingRow extends StatefulWidget {
  const PlayingRow({
    required this.handle,
    required this.soundLength,
    required this.onStopped,
    super.key,
  });

  final double soundLength;
  final SoundHandle handle;
  final VoidCallback onStopped;

  @override
  State<PlayingRow> createState() => _PlayingRowState();
}

class _PlayingRowState extends State<PlayingRow> {
  final ValueNotifier<bool> isPaused = ValueNotifier(true);
  final ValueNotifier<double> soundPosition = ValueNotifier(0);
  Timer? timer;

  @override
  void dispose() {
    stopTimer();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        ValueListenableBuilder<bool>(
          valueListenable: isPaused,
          builder: (_, paused, __) {
            if (paused) {
              startTimer();
            } else {
              stopTimer();
            }
            return IconButton(
              onPressed: () async {
                SoLoud.instance.pauseSwitch(widget.handle);
                isPaused.value = SoLoud.instance.getPause(widget.handle).pause;
              },
              icon: paused
                  ? const Icon(Icons.pause_circle_outline, size: 48)
                  : const Icon(Icons.play_circle_outline, size: 48),
              iconSize: 48,
            );
          },
        ),
        const SizedBox(width: 16),
        IconButton(
          onPressed: () async {
            await SoLoud.instance.stop(widget.handle);
            widget.onStopped();
          },
          icon: const Icon(Icons.stop_circle_outlined, size: 48),
          iconSize: 48,
        ),

        /// Seek slider
        Expanded(
          child: ValueListenableBuilder<double>(
            valueListenable: soundPosition,
            builder: (_, position, __) {
              if (position >= widget.soundLength) {
                position = 0;
              }

              return Row(
                children: [
                  Text(position.toStringAsFixed(1)),
                  Expanded(
                    child: Slider(
                      value: position,
                      max: widget.soundLength < position
                          ? position
                          : widget.soundLength,
                      onChanged: (value) {
                        soundPosition.value = value;
                        SoLoud.instance.seek(widget.handle, value);
                      },
                    ),
                  ),
                  Text(widget.soundLength.toInt().toString()),
                ],
              );
            },
          ),
        ),
      ],
    );
  }

  /// start timer to update the audio position slider
  void startTimer() {
    timer?.cancel();
    timer = Timer.periodic(const Duration(milliseconds: 100), (_) {
      soundPosition.value = SoLoud.instance.getPosition(widget.handle).position;
    });
  }

  /// stop timer
  void stopTimer() {
    timer?.cancel();
  }
}

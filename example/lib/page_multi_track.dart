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

    return const Scaffold(
      body: Padding(
        padding: EdgeInsets.only(top: 50, right: 8, left: 8),
        child: Column(
          // mainAxisAlignment: MainAxisAlignment.start,

          children: [
            Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                PlaySoundWidget(
                  assetsAudio: 'assets/audio/8_bit_mentality.mp3',
                  text: '8 bit mentality',
                ),
                SizedBox(height: 32),
                PlaySoundWidget(
                  assetsAudio: 'assets/audio/explosion.mp3',
                  text: 'game explosion',
                ),
              ],
            ),
            SizedBox(height: 12),
          ],
        ),
      ),
    );
  }
}

class PlaySoundWidget extends StatefulWidget {
  const PlaySoundWidget({
    required this.assetsAudio,
    required this.text,
    super.key,
  });

  final String assetsAudio;
  final String text;

  @override
  State<PlaySoundWidget> createState() => _PlaySoundWidgetState();
}

class _PlaySoundWidgetState extends State<PlaySoundWidget> {
  static final Logger _log = Logger('_PlaySoundWidgetState');

  late double soundLength;
  final Map<int, ValueNotifier<bool>> isPaused = {};
  final Map<int, ValueNotifier<double>> soundPosition = {};
  StreamSubscription<StreamSoundEvent>? _subscription;
  SoundProps? sound;

  @override
  void dispose() {
    _subscription?.cancel();
    super.dispose();
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
          for (int i = 0; i < sound!.handle.length; ++i)
            PlayingRow(
              handle: sound!.handle.elementAt(i),
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

    final newHandle = await SoLoud.instance.play(sound!);
    if (newHandle.error == PlayerErrors.noError) return;

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
  final int handle;
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
                  Text(position.toInt().toString()),
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

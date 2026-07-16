import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart' show rootBundle;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/pull_buffer/seek_bar.dart';
import 'package:logging/logging.dart';

/// This example reads a local audio file in chunks and feeds it to an
/// [AudioSource]. It demonstrates the callback-driven pull model and the custom
/// seek/progress bar.
///
/// On the web, [AudioSource] cannot read arbitrary local files, so this
/// example loads a bundled asset instead.

void main() async {
  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
  });

  WidgetsFlutterBinding.ensureInitialized();
  await SoLoud.instance.init();
  runApp(const MaterialApp(home: FileStreamExample()));
}

class FileStreamExample extends StatefulWidget {
  const FileStreamExample({super.key});

  @override
  State<FileStreamExample> createState() => _FileStreamExampleState();
}

class _FileStreamExampleState extends State<FileStreamExample> {
  /// Size of each encoded chunk read from the asset. The chunk size should be
  /// smaller than the file size so that `onMoreDataIsNeeded` is called multiple
  /// times and the pull-buffer behavior is visible. If the chunk is larger than
  /// the whole file, the entire file is fed in one callback and the circular
  /// buffer just consumes what was already provided.
  ///
  /// At ~128 kbps MP3, 128 KB is roughly 8 seconds of audio, which decodes to
  /// ~2.8 MB of float PCM and fits comfortably in the 5 MB circular buffer
  /// below.
  static const _chunkSize = 10 * 1024;

  /// Decoded circular buffer size. At 44.1 kHz stereo float PCM this holds
  /// roughly 14 seconds of audio. The encoded chunk size is independent of this
  /// value because decoded samples that do not fit are queued for later.
  static const _bufferSizeBytes = 5 * 1024 * 1024;
  static const _bufferTriggerPosition = 0.75;

  static const audioMP3 = 'assets/audio/sample-MP3.mp3';
  static const audioFLAC = 'assets/audio/sample-FLAC.flac';
  static const audioOPUS = 'assets/audio/sample-OPUS.opus';
  static const audioVORBIS = 'assets/audio/sample-vorbis.ogg';
  static const audioWAV = 'assets/audio/sample-WAV.wav';

  static final _logger = Logger('PullBufferFileStream');

  AudioSource? _source;
  SoundHandle? _handle;
  Uint8List? _fileBytes;
  int _totalBytes = 0;
  final _fetchedOffsets = <int>{};
  Duration _duration = Duration.zero;
  Duration _position = Duration.zero;
  Duration _bufferedStart = Duration.zero;
  Duration _bufferedEnd = Duration.zero;
  int _sampleRate = 44100;
  int _channels = 2;
  bool _isPlaying = false;
  Timer? _timer;
  Timer? _flashTimer;
  Color? _flashColor;
  String _status = 'Tap Load sample MP3 to start';

  @override
  void dispose() {
    _timer?.cancel();
    _flashTimer?.cancel();
    SoLoud.instance.deinit();
    super.dispose();
  }

  void _startTicker() {
    _timer?.cancel();
    _timer = Timer.periodic(const Duration(milliseconds: 50), (_) {
      if (_handle == null) return;
      if (!SoLoud.instance.getIsValidVoiceHandle(_handle!)) {
        _timer?.cancel();
        setState(() => _isPlaying = false);
        return;
      }
      final newPos = SoLoud.instance.getPosition(_handle!);
      final range = SoLoud.instance.getPullBufferTimeRange(_source!);
      // The red bar shows the circular-buffer window. The window is steady
      // while the playhead advances; it only shifts when new decoded data
      // is added after the trigger position is reached. The playhead can
      // seek anywhere inside the red bar as long as the decoded samples are
      // still in the buffer.
      setState(() {
        _position = newPos;
        _bufferedStart = range.startTime;
        _bufferedEnd = range.endTime;
      });
    });
  }

  Future<void> _loadFromAsset(String asset) async {
    _logger.info('Loading asset: $asset');
    final data = await rootBundle.load(asset);
    _fileBytes = data.buffer.asUint8List();
    _totalBytes = _fileBytes!.length;
    _logger.info('Asset loaded: $_totalBytes bytes');
    await _startStream();
  }

  Future<void> _startStream() async {
    _timer?.cancel();
    _flashTimer?.cancel();

    final oldHandle = _handle;
    _handle = null;
    _source = null;
    _fetchedOffsets.clear();
    _bufferedStart = Duration.zero;
    _bufferedEnd = Duration.zero;
    _position = Duration.zero;
    _flashColor = null;

    if (oldHandle != null && SoLoud.instance.getIsValidVoiceHandle(oldHandle)) {
      await SoLoud.instance.stop(oldHandle);
    }

    // On the web, disposing the old source while the audio thread is still
    // processing it can cause a segfault or mutex deadlock. We leave the old
    // source in the engine; it will be cleaned up when the widget is disposed.
    // Each callback closure captures its own source reference so only the
    // current source gets data.
    AudioSource? currentSource;
    _source = currentSource = SoLoud.instance.setPullBufferStream(
      bufferSizeBytes: _bufferSizeBytes,
      bufferTriggerPosition: _bufferTriggerPosition,
      audioSizeBytes: _totalBytes,
      onMetadata: (metadata) {
        _logger.info('onMetadata: detectedType=${metadata.detectedType} ');
      },
      onAudioDuration: (duration) {
        if (currentSource != _source || !mounted) return;
        _logger.info('onAudioDuration: ${duration}s');
        setState(
          () => _duration = Duration(
            milliseconds: (duration * 1000).round(),
          ),
        );
      },
      onMoreDataIsNeeded: (offset) {
        if (currentSource != _source ||
            currentSource == null ||
            _fileBytes == null ||
            offset < 0 ||
            offset >= _totalBytes ||
            !mounted) {
          _logger.fine(
            'onMoreDataIsNeeded: offset $offset out of range or stale, '
            'skipping',
          );
          return;
        }
        if (!_fetchedOffsets.add(offset)) {
          _logger.fine('onMoreDataIsNeeded: offset $offset already fetched');
          return;
        }
        final end = (offset + _chunkSize).clamp(0, _totalBytes);
        final chunk = _fileBytes!.sublist(offset, end);
        _logger.info(
          'onMoreDataIsNeeded: offset=$offset end=$end total=$_totalBytes '
          'chunk=${chunk.length}',
        );

        // print('*********************************************************** '
        //     '_bufferedStart=${_bufferedStart.inMilliseconds}ms '
        //     '_bufferedEnd=${_bufferedEnd.inMilliseconds}ms '
        //     'position=${_position.inMilliseconds}ms');

        SoLoud.instance.addPullBufferDataStream(
          currentSource,
          chunk,
          offset: offset,
        );
        if (mounted) {
          setState(() {
            _status = 'Feeding $end / $_totalBytes bytes';
          });
        }
      },
    );

    _handle = SoLoud.instance.play(_source!);
    _startTicker();
    setState(() {
      _isPlaying = true;
      _status = 'Playing from local file';
    });
    _logger.info('Started playback, handle=$_handle');
  }

  void _onSeek(Duration pos) {
    if (_handle == null) return;
    final isSmart = _bufferedEnd > Duration.zero &&
        pos >= _bufferedStart &&
        pos <= _bufferedEnd;
    final flashColor = isSmart ? Colors.green : Colors.red;
    _logger.info(
      'Seek ${isSmart ? 'smart' : 'out-of-buffer'} to '
      '${pos.inMilliseconds}ms flashColor=$flashColor '
      'window=[${_bufferedStart.inMilliseconds}, '
      '${_bufferedEnd.inMilliseconds}]',
    );
    setState(() {
      _flashColor = flashColor;
      if (!isSmart) {
        _fetchedOffsets.clear();
      }
    });
    SoLoud.instance.seek(_handle!, pos);
    _flashTimer?.cancel();
    _flashTimer = Timer(const Duration(milliseconds: 500), () {
      if (mounted) {
        setState(() => _flashColor = null);
      }
    });
  }

  void _togglePause() {
    if (_handle == null) return;
    SoLoud.instance.setPause(_handle!, _isPlaying);
    setState(() => _isPlaying = !_isPlaying);
    _logger.info(_isPlaying ? 'Paused' : 'Resumed');
  }

  void _reset() {
    if (_source == null || _fileBytes == null) return;
    _logger.info('Restarting pull buffer stream');
    _fetchedOffsets.clear();
    _bufferedStart = Duration.zero;
    _bufferedEnd = Duration.zero;
    _position = Duration.zero;
    _flashColor = null;
    _flashTimer?.cancel();
    unawaited(_startStream());
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Pull buffer from local file')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          spacing: 16,
          children: [
            Text(_status),
            Wrap(
              alignment: WrapAlignment.center,
              children: [
                ElevatedButton(
                  onPressed: () => _loadFromAsset(audioMP3),
                  child: const Text('Load sample MP3'),
                ),
                ElevatedButton(
                  onPressed: () => _loadFromAsset(audioFLAC),
                  child: const Text('Load sample FLAC'),
                ),
                ElevatedButton(
                  onPressed: () => _loadFromAsset(audioOPUS),
                  child: const Text('Load sample OPUS'),
                ),
                ElevatedButton(
                  onPressed: () => _loadFromAsset(audioVORBIS),
                  child: const Text('Load sample VORBIS'),
                ),
                ElevatedButton(
                  onPressed: () => _loadFromAsset(audioWAV),
                  child: const Text('Load sample WAV'),
                ),
              ],
            ),
            if (_source != null) ...[
              PullBufferSeekBar(
                duration: _duration,
                bufferedStart: _bufferedStart,
                bufferedEnd: _bufferedEnd,
                position: _position,
                flashColor: _flashColor,
                onSeek: _onSeek,
              ),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  IconButton(
                    onPressed: _togglePause,
                    icon: Icon(_isPlaying ? Icons.pause : Icons.play_arrow),
                  ),
                  IconButton(
                    onPressed: _reset,
                    icon: const Icon(Icons.replay),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

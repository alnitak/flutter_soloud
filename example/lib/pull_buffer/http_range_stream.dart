import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/pull_buffer/seek_bar.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

/// This example streams an audio file from a remote server using HTTP Range
/// requests and feeds it to an [AudioSource]. It demonstrates the
/// callback-driven pull model and the custom seek/progress bar.
///
/// The server must support HTTP Range requests and CORS (when running on web).
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
  runApp(const MaterialApp(home: HttpRangeStreamExample()));
}

class HttpRangeStreamExample extends StatefulWidget {
  const HttpRangeStreamExample({super.key});

  @override
  State<HttpRangeStreamExample> createState() => _HttpRangeStreamExampleState();
}

class _HttpRangeStreamExampleState extends State<HttpRangeStreamExample> {
  /// Size of each encoded chunk fetched from the remote server. 64 KB is a
  /// reasonable default for HTTP: it keeps request overhead low while still
  /// allowing the pull-buffer callback to fire several times for a typical
  /// song.
  static const _chunkSize = 64 * 1024;

  /// Decoded circular buffer size. At 44.1 kHz stereo float PCM this holds
  /// roughly 14 seconds of audio. Decoded samples that do not fit are queued
  /// for later.
  static const _bufferSizeBytes = 5 * 1024 * 1024;
  static const _bufferTriggerPosition = 0.75;

  static const _defaultUrl =
      'https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3';
  static const _defaultMP3Url = 'http://localhost:8088/sample-MP3.mp3';
  static const _defaultFLACUrl = 'http://localhost:8088/sample-FLAC.flac';
  static const _defaultOPUSUrl = 'http://localhost:8088/sample-OPUS.opus';
  static const _defaultVORBISUrl = 'http://localhost:8088/sample-VORBIS.ogg';
  static const _defaultWAVUrl = 'http://localhost:8088/sample-WAV.wav';

  static final _logger = Logger('PullBufferHttpRangeStream');

  final _client = http.Client();
  final _urlController = TextEditingController(text: _defaultUrl);

  AudioSource? _source;
  SoundHandle? _handle;
  int _totalBytes = 0;
  Uint8List? _fileBytes;
  final _fetchedOffsets = <int>{};
  final _pendingOffsets = <int>{};
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
  String _status = 'Enter a URL and tap Load stream';

  @override
  void dispose() {
    _timer?.cancel();
    _flashTimer?.cancel();
    _client.close();
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
      setState(() {
        _position = newPos;
        _bufferedStart = range.startTime;
        _bufferedEnd = range.endTime;
      });
    });
  }

  Future<int> _fetchContentLength(String url) async {
    try {
      final response = await _client.head(Uri.parse(url));
      if (response.statusCode < 200 || response.statusCode >= 300) {
        _logger.warning('HEAD returned ${response.statusCode}');
        return 0;
      }
      final acceptRanges = response.headers['accept-ranges'];
      final serverSupportsRange = acceptRanges == 'bytes';
      _logger.info(
        serverSupportsRange ? 'Server supports HTTP Range requests' :
            'Server does not support HTTP Range requests. '
            'Audio will not get the audio duration for OGG audios.\n',
        'Accept-Ranges: $acceptRanges; supportsRange=$serverSupportsRange',
      );
      final length = response.headers['content-length'];
      if (length != null && length.isNotEmpty) {
        return int.parse(length);
      }
    } catch (e, st) {
      _logger
        ..warning('HEAD failed: $e')
        ..fine(st.toString());
    }
    return 0;
  }

  Future<void> _loadStream(String? url) async {
    final newUrl = url ?? _defaultUrl;

    // Stop any previous playback and clean up the old stream before starting a
    // new one. This prevents concurrent requests from overlapping and avoids
    // feeding data into the wrong [AudioSource].
    if (_handle != null) {
      unawaited(SoLoud.instance.stop(_handle!));
      _handle = null;
    }
    _source = null;
    _timer?.cancel();
    _fileBytes = null;
    _fetchedOffsets.clear();
    _pendingOffsets.clear();
    _bufferedStart = Duration.zero;
    _bufferedEnd = Duration.zero;
    _duration = Duration.zero;
    _position = Duration.zero;

    _totalBytes = await _fetchContentLength(newUrl);
    _urlController.text = newUrl;

    if (_totalBytes <= 0) {
      setState(() => _status = 'Could not determine content length');
      return;
    }

    _source = SoLoud.instance.setPullBufferStream(
      bufferSizeBytes: _bufferSizeBytes,
      bufferTriggerPosition: _bufferTriggerPosition,
      audioSizeBytes: _totalBytes,
      onMetadata: _onMetadata,
      onAudioDuration: (duration) {
        _logger.info('onAudioDuration: ${duration}s');
        setState(
          () => _duration = Duration(
            milliseconds: (duration * 1000).round(),
          ),
        );
      },
      onMoreDataIsNeeded: (offset) => _fetchChunk(newUrl, offset),
    );

    _handle = SoLoud.instance.play(_source!);
    _startTicker();
    setState(() {
      _isPlaying = true;
    });
  }

  void _onMetadata(AudioMetadata metadata) {
    var sampleRate = _sampleRate;
    var channels = _channels;
    switch (metadata.detectedType) {
      case DetectedType.oggVorbis:
        sampleRate = metadata.oggMetadata?.vorbisInfo.rate ?? sampleRate;
        channels = metadata.oggMetadata?.vorbisInfo.channels ?? channels;
      case DetectedType.oggOpus:
        sampleRate =
            metadata.oggMetadata?.opusInfo.inputSampleRate ?? sampleRate;
        channels = metadata.oggMetadata?.opusInfo.channels ?? channels;
      case DetectedType.oggFlac:
        sampleRate = metadata.oggMetadata?.flacInfo.sampleRate ?? sampleRate;
        channels = metadata.oggMetadata?.flacInfo.channels ?? channels;
      case DetectedType.mp3WithId3:
      case DetectedType.mp3Stream:
      case DetectedType.wav:
      case DetectedType.unknown:
        break;
    }
    _logger.info(
      'onMetadata: detectedType=${metadata.detectedType} '
      'sampleRate=$sampleRate channels=$channels',
    );
    setState(() {
      _sampleRate = sampleRate;
      _channels = channels;
    });
  }

  Future<void> _fetchChunk(String url, int offset) async {
    if (_totalBytes > 0 && offset >= _totalBytes) {
      return;
    }
    if (!_pendingOffsets.add(offset)) {
      _logger.fine('offset $offset already in flight, skipping');
      return;
    }

    try {
      final end = (offset + _chunkSize - 1).clamp(0, _totalBytes - 1);
      final request = http.Request('GET', Uri.parse(url))
        ..headers['Range'] = 'bytes=$offset-$end';
      _logger.info('Fetching range bytes=$offset-$end');
      final streamedResponse = await _client.send(request);

      if (streamedResponse.statusCode != 206 &&
          streamedResponse.statusCode != 200) {
        setState(() => _status = 'HTTP ${streamedResponse.statusCode}');
        _pendingOffsets.remove(offset);
        return;
      }

      final bytes = await streamedResponse.stream.toBytes();
      if (bytes.isEmpty) {
        _pendingOffsets.remove(offset);
        return;
      }

      _fetchedOffsets.add(offset);
      SoLoud.instance.addPullBufferDataStream(
        _source!,
        bytes,
        offset: offset,
      );
      setState(() {
        final endPosition = offset + bytes.length;
        _status = 'Buffered $endPosition / $_totalBytes bytes';
      });
    } catch (e, st) {
      _logger
        ..warning('Network error: $e')
        ..fine(st.toString());
    } finally {
      _pendingOffsets.remove(offset);
    }
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
        _pendingOffsets.clear();
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
    _logger.info(_isPlaying ? 'Resumed' : 'Paused');
  }

  void _reset() {
    if (_source == null) return;
    _logger.info('Reset pull buffer stream');
    SoLoud.instance.resetPullBufferStream(_source!);
    setState(() {
      _fetchedOffsets.clear();
      _pendingOffsets.clear();
      _bufferedStart = Duration.zero;
      _bufferedEnd = Duration.zero;
      _position = Duration.zero;
      _flashColor = null;
    });
    _flashTimer?.cancel();
  }

  @override
  Widget build(BuildContext context) {
    // If the duration is still unknown, use the buffered end as a minimum so
    // the seek bar has a non-zero scale and the red bar is visible.
    final displayDuration = _duration > _bufferedEnd ? _duration : _bufferedEnd;

    return Scaffold(
      appBar: AppBar(title: const Text('Pull buffer from HTTP Range')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          spacing: 16,
          children: [
            TextField(
              controller: _urlController,
              decoration: const InputDecoration(
                labelText: 'Audio URL',
                hintText: 'https://example.com/audio.mp3',
              ),
              enabled: _source == null,
            ),
            Text(_status),
            Wrap(
              children: [
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultUrl),
                  child: const Text('Load stream'),
                ),
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultMP3Url),
                  child: const Text('MP3 stream'),
                ),
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultFLACUrl),
                  child: const Text('FLAC stream'),
                ),
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultOPUSUrl),
                  child: const Text('OPUS stream'),
                ),
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultVORBISUrl),
                  child: const Text('VORBIS stream'),
                ),
                ElevatedButton(
                  onPressed: () => _loadStream(_defaultWAVUrl),
                  child: const Text('WAV stream'),
                ),
              ],
            ),
            if (_source != null) ...[
              PullBufferSeekBar(
                duration: displayDuration,
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

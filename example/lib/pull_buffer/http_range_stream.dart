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
  static const _chunkSize = 65536;

  final _client = http.Client();
  final _fetchedOffsets = <int>{};
  final _pendingOffsets = <int>{};
  final _urlController = TextEditingController(
    text: 'https://www.soundhelix.com/examples/mp3/SoundHelix-Song-1.mp3',
  );

  AudioSource? _source;
  SoundHandle? _handle;
  int _totalBytes = 0;
  Duration _duration = Duration.zero;
  Duration _position = Duration.zero;
  Duration _bufferedStart = Duration.zero;
  Duration _bufferedEnd = Duration.zero;
  bool _isPlaying = false;
  Timer? _timer;
  String _status = 'Enter a URL and tap Load';

  @override
  void dispose() {
    _timer?.cancel();
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
      setState(() {
        _position = SoLoud.instance.getPosition(_handle!);
        _duration = SoLoud.instance.getLength(_source!);
      });
    });
  }

  Future<int> _fetchContentLength(String url) async {
    try {
      final response = await _client.head(Uri.parse(url));
      final length = response.headers['content-length'];
      if (length != null && length.isNotEmpty) {
        return int.parse(length);
      }
    } catch (e) {
      debugPrint('HEAD failed: $e');
    }
    return 0;
  }

  Future<void> _loadStream() async {
    final url = _urlController.text.trim();
    if (url.isEmpty) return;

    _totalBytes = await _fetchContentLength(url);
    _fetchedOffsets.clear();
    _pendingOffsets.clear();
    _bufferedEnd = Duration.zero;

    _source = SoLoud.instance.setPullBufferStream(
      audioSizeBytes: _totalBytes,
      onAudioDuration: (duration) {
        setState(() {
          _duration = Duration(
            milliseconds: (duration * 1000).round(),
          );
        });
      },
      onMoreDataIsNeeded: (offset) => _fetchChunk(url, offset),
    );

    _handle = SoLoud.instance.play(_source!);
    _startTicker();
    setState(() {
      _isPlaying = true;
      _status = _totalBytes > 0
          ? 'Streaming $_totalBytes bytes via HTTP Range'
          : 'Streaming (unknown length)';
    });
  }

  Future<void> _fetchChunk(String url, int offset) async {
    if (_totalBytes > 0 && offset >= _totalBytes) {
      SoLoud.instance.setPullBufferDataIsEnded(_source!);
      return;
    }
    if (!_pendingOffsets.add(offset)) return;

    try {
      final end = _totalBytes > 0
          ? (offset + _chunkSize - 1).clamp(0, _totalBytes - 1)
          : offset + _chunkSize - 1;
      final request = http.Request('GET', Uri.parse(url))
        ..headers['Range'] = 'bytes=$offset-$end';
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
      final range = SoLoud.instance.getPullBufferTimeRange(_source!);
      final endPosition = offset + bytes.length;
      setState(() {
        _bufferedStart = range.startTime;
        _bufferedEnd = range.endTime;
        _status = 'Buffered $endPosition / $_totalBytes bytes';
      });

      if (_totalBytes > 0 && endPosition >= _totalBytes) {
        SoLoud.instance.setPullBufferDataIsEnded(_source!);
      } else if (bytes.length < _chunkSize) {
        SoLoud.instance.setPullBufferDataIsEnded(_source!);
      }
    } catch (e, st) {
      setState(() => _status = 'Network error: $e');
      debugPrintStack(stackTrace: st);
    } finally {
      _pendingOffsets.remove(offset);
    }
  }

  @override
  Widget build(BuildContext context) {
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
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                ElevatedButton(
                  onPressed: _source == null ? _loadStream : null,
                  child: const Text('Load stream'),
                ),
              ],
            ),
            if (_source != null) ...[
              PullBufferSeekBar(
                duration: _duration,
                bufferedStart: _bufferedStart,
                bufferedEnd: _bufferedEnd,
                position: _position,
                onSeek: (pos) {
                  if (_handle != null) {
                    SoLoud.instance.seek(_handle!, pos);
                  }
                },
              ),
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  IconButton(
                    onPressed: () {
                      if (_handle == null) return;
                      SoLoud.instance.setPause(_handle!, _isPlaying);
                      setState(() => _isPlaying = !_isPlaying);
                    },
                    icon: Icon(_isPlaying ? Icons.pause : Icons.play_arrow),
                  ),
                  IconButton(
                    onPressed: () {
                      if (_source == null) return;
                      SoLoud.instance.resetPullBufferStream(_source!);
                      setState(() {
                        _fetchedOffsets.clear();
                        _pendingOffsets.clear();
                        _bufferedEnd = Duration.zero;
                        _position = Duration.zero;
                      });
                    },
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

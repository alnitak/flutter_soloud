import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/buffer_widget.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

void main() async {
  // The `flutter_soloud` package logs everything
  // (from severe warnings to fine debug messages)
  // using the standard `package:logging`.
  // You can listen to the logs as shown below.
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

  /// Initialize the player.
  await SoLoud.instance.init();

  runApp(
    const MaterialApp(
      home: WebRadioExample(),
    ),
  );
}

class WebRadioExample extends StatefulWidget {
  const WebRadioExample({super.key});

  @override
  State<WebRadioExample> createState() => _WebRadioExampleState();
}

class _WebRadioExampleState extends State<WebRadioExample> {
  AudioSource? source;
  final streamBuffering = ValueNotifier(false);

  // https://dir.xiph.org/codecs
  /// MP3s
  final mp3Urls = [
    'http://as.fm1.be:8000/media',
    'http://as.fm1.be:8000/rand',
    'http://as.fm1.be:8000/vlar1.mp3',
    'http://xfer.hirschmilch.de:8000/techno.mp3',
  ];

  /// OGGs
  final oggUrls = [
    'http://streaming.cuacfm.org/cuacfm.ogg',
    'http://superaudio.radio.br:8074/stream',
    'http://icecast.ithost.it:8000/retesport.ogg',
  ];

  /// OPUSes
  final opusUrls = [
    'http://radio.glafir.ru:7000/pop-mix',
    'http://icecast.err.ee/klarajazz.opus',
  ];

  int mp3UrlId = 0;
  int oggUrlId = 0;
  int opusUrlId = 0;
  http.Client? _client;
  http.StreamedResponse? _currentStream;
  final connectionError = ValueNotifier<String>('');

  Future<void> connectToUrl(String url) async {
    connectionError.value = '';
    try {
      // Cancel any existing connection
      // await _currentStream?.stream.drain<void>();
      _currentStream = null;
      _client?.close();
      _client = null;

      // Create a new client and request
      _client = http.Client();
      final request = http.Request('GET', Uri.parse(url));
      _currentStream = await _client!.send(request);

      // Handle redirections
      if (_currentStream!.statusCode == 301 ||
          _currentStream!.statusCode == 302) {
        final redirectUrl = _currentStream!.headers['location'];
        if (redirectUrl != null) {
          // Close current connection and try with new URL
          await _currentStream!.stream.drain<void>();
          _currentStream = null;
          _client!.close();
          _client = null;
          await connectToUrl(redirectUrl);
          return;
        }
      }

      // Check for successful connection
      if (_currentStream!.statusCode != 200) {
        connectionError.value = 'error: ${_currentStream!.statusCode} - '
            '${_currentStream!.reasonPhrase}';

        _currentStream = null;
        _client?.close();
        _client = null;
        return;
      }

      // Listen to the stream and feed data to the audio source
      _currentStream!.stream.listen(
        (data) {
          if (source != null) {
            SoLoud.instance
                .addAudioDataStream(source!, Uint8List.fromList(data));
          }
        },
        onError: (Object error) {
          debugPrint('Stream error: $error');
          _client?.close();
          _client = null;
        },
        onDone: () {
          debugPrint('Stream closed');
          _client?.close();
          _client = null;
        },
      );
    } catch (e) {
      debugPrint('Connection error: $e');
      _client?.close();
      _client = null;
      rethrow;
    }
  }

  Future<void> playUrl(String url, BufferType type) async {
    await SoLoud.instance.disposeAllSources();
    streamBuffering.value = true;
    source = SoLoud.instance.setBufferStream(
      bufferingTimeNeeds: 1,
      format: type,
      bufferingType: BufferingType.released,
      channels: Channels.stereo,
      onBuffering: (isBuffering, handle, time) async {
        debugPrint('started buffering? $isBuffering  with '
            'handle: $handle at time $time');
        streamBuffering.value = isBuffering;
      },
    );

    await SoLoud.instance.play(source!);

    unawaited(connectToUrl(url));
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          spacing: 16,
          children: [
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text('MP3 radio:  '),
                DropdownButton(
                  value: mp3UrlId,
                  items: List.generate(mp3Urls.length, (index) {
                    return DropdownMenuItem(
                      value: index,
                      child: Row(
                        children: [
                          Text(
                            mp3Urls[index],
                            style: const TextStyle(fontWeight: FontWeight.bold),
                          ),
                        ],
                      ),
                    );
                  }),
                  onChanged: (value) {
                    mp3UrlId = value ?? 0;
                    playUrl(mp3Urls[mp3UrlId], BufferType.mp3);
                    if (context.mounted) {
                      setState(() {});
                    }
                  },
                ),
              ],
            ),
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text('OGG radio:  '),
                DropdownButton(
                  value: oggUrlId,
                  items: List.generate(oggUrls.length, (index) {
                    return DropdownMenuItem(
                      value: index,
                      child: Row(
                        children: [
                          Text(
                            oggUrls[index],
                            style: const TextStyle(fontWeight: FontWeight.bold),
                          ),
                        ],
                      ),
                    );
                  }),
                  onChanged: (value) {
                    oggUrlId = value ?? 0;
                    playUrl(oggUrls[oggUrlId], BufferType.opus);
                    if (context.mounted) {
                      setState(() {});
                    }
                  },
                ),
              ],
            ),
            Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                const Text('OPUS radio:  '),
                DropdownButton(
                  value: opusUrlId,
                  items: List.generate(opusUrls.length, (index) {
                    return DropdownMenuItem(
                      value: index,
                      child: Row(
                        children: [
                          Text(
                            opusUrls[index],
                            style: const TextStyle(fontWeight: FontWeight.bold),
                          ),
                        ],
                      ),
                    );
                  }),
                  onChanged: (value) {
                    opusUrlId = value ?? 0;
                    playUrl(opusUrls[opusUrlId], BufferType.opus);
                    if (context.mounted) {
                      setState(() {});
                    }
                  },
                ),
              ],
            ),
            ValueListenableBuilder(
              valueListenable: streamBuffering,
              builder: (context, value, child) {
                return Column(
                  spacing: 16,
                  children: [
                    BufferBar(
                      bufferingType: BufferingType.released,
                      isBuffering: value,
                      sound: source,
                    ),
                    ValueListenableBuilder(
                      valueListenable: streamBuffering,
                      builder: (_, value, __) {
                        if (value) {
                          return const Text('BUFFERING!');
                        }
                        return const SizedBox.shrink();
                      },
                    ),
                  ],
                );
              },
            ),
            ValueListenableBuilder(
              valueListenable: connectionError,
              builder: (context, error, child) {
                if (error.isNotEmpty) {
                  return Text(
                    'Connection Error: $error',
                    style: const TextStyle(
                      fontWeight: FontWeight.bold,
                      color: Colors.red,
                    ),
                  );
                }
                return const SizedBox.shrink();
              },
            ),
          ],
        ),
      ),
    );
  }
}

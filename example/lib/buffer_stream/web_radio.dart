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
  await SoLoud.instance.init(sampleRate: 24000);

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
  // https://dir.xiph.org/codecs
  /// MP3s
  final mp3Urls = [
    'http://as.fm1.be:8000/media',
    'http://as.fm1.be:8000/rand',
    'http://as.fm1.be:8000/vlar1.mp3',
    'http://xfer.hirschmilch.de:8000/techno.mp3',
    'http://as.fm1.be:8000/wrgm1',
    'http://stream.danubiusradio.hu:8081/danubius_192k',
    'http://live.coolradio.rs/cool128',
    'http://stream.lazaradio.com:8100/live.mp3',
    'http://www.appradio.app:8010/live',
    'http://streaming.radiominerva.be:8000/minerva',
    'http://ice37.fluidstream.net/ric.mp3',
  ];

  /// OGGs
  final oggUrls = [
    'http://play.global.audio/nova.ogg',
    'http://superaudio.radio.br:8074/stream',
    'http://stream.lazaradio.com:8100/live.ogg',
    'http://stream.trendyradio.pl:8000/m',
    'http://superaudio.radio.br:8074/stream',
    'http://play.global.audio/nrj.ogg',
    'http://play.global.audio/radio1rock.ogg',
    'http://stream.danubiusradio.hu:8091/danubius_HiFi',
  ];

  /// OPUSes
  final opusUrls = [
    'http://localhost:8080',
    'http://radio.glafir.ru:7000/pop-mix',
    'http://icecast.err.ee/klarajazz.opus',
    'http://xfer.hirschmilch.de:8000/prog-house.opus',
    'http://emisoras.dip-badajoz.es:8239/stream',
    'http://icecast.walmradio.com:8000/otr_opus',
    'http://xfer.hirschmilch.de:8000/techno.opus',
    'http://icecast.err.ee/raadio2.opus',
    'http://radio.glafir.ru:7000/humor',
    'http://icecast.err.ee/vikerraadio.opus',
    'http://radio.glafir.ru:7000/classic',
    'http://radio.glafir.ru:7000/easy-listen',
  ];

  int mp3UrlId = 0;
  int oggUrlId = 0;
  int opusUrlId = 0;
  AudioSource? source;
  final streamBuffering = ValueNotifier(false);
  final TextEditingController urlController = TextEditingController(text: '');
  final connectionError = ValueNotifier<String>('');
  http.Client? client;
  http.StreamedResponse? currentStream;

  Future<void> connectToUrl(String url) async {
    connectionError.value = '';
    urlController.text = url;
    try {
      // Cancel any existing connection
      currentStream = null;
      client?.close();
      client = null;

      // Create a new client and request
      client = http.Client();
      final request = http.Request('GET', Uri.parse(url));
      currentStream = await client!.send(request);

      // Handle redirections
      if (currentStream!.statusCode == 301 ||
          currentStream!.statusCode == 302) {
        final redirectUrl = currentStream!.headers['location'];
        if (redirectUrl != null) {
          // Close current connection and try with new URL
          await currentStream!.stream.drain<void>();
          currentStream = null;
          client!.close();
          client = null;
          await connectToUrl(redirectUrl);
          return;
        }
      }

      // Check for successful connection
      if (currentStream!.statusCode != 200) {
        connectionError.value = 'error: ${currentStream!.statusCode} - '
            '${currentStream!.reasonPhrase}';

        currentStream = null;
        client?.close();
        client = null;
        return;
      }

      // Listen to the stream and feed data to the audio source
      currentStream!.stream.listen(
        (data) {
          if (source != null) {
            try {
              SoLoud.instance
                  .addAudioDataStream(source!, Uint8List.fromList(data));
            } on SoLoudStreamEndedAlreadyCppException catch (e) {
              debugPrint('The buffer has been filled: $e');
              client?.close();
              client = null;
            } catch (e) {
              debugPrint('Error adding audio data: $e');
            }
          }
        },
        onError: (Object error) {
          connectionError.value = 'Stream error: $error';
          debugPrint('Stream error: $error');
          client?.close();
          client = null;
        },
        onDone: () {
          connectionError.value = 'Stream closed.';
          debugPrint('Stream closed. Maybe a stream changed track?');
          client?.close();
          client = null;
          // Some streams may close automatically after a song ends.
          // You can try to reconnect or just leave it.
          Future.delayed(const Duration(milliseconds: 500), () {
            unawaited(connectToUrl(url));
          });
        },
      );
    } catch (e) {
      connectionError.value = 'Connection error: $e';
      debugPrint('Connection error: $e');
      client?.close();
      client = null;
      rethrow;
    }
  }

  Future<void> playUrl(String url, BufferType type) async {
    streamBuffering.value = true;
    source = SoLoud.instance.setBufferStream(
      maxBufferSizeBytes: 1024 * 1024 * 200, // 100 MB
      bufferingTimeNeeds: 3,
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
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Center(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 30,
            children: [
              TextField(
                controller: urlController,
                decoration: const InputDecoration(
                  labelText: 'Choose below or enter a URL to stream',
                  hintText: 'e.g. http://example.com/stream.mp3',
                ),
                onSubmitted: (value) {
                  if (value.isNotEmpty) {
                    playUrl(value, BufferType.mp3);
                  }
                },
              ),
              Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'MP3 radio:  ',
                    style: TextStyle(fontWeight: FontWeight.bold),
                  ),
                  DropdownButton(
                    value: mp3UrlId,
                    items: List.generate(mp3Urls.length, (index) {
                      return DropdownMenuItem(
                        value: index,
                        child: Text(
                          mp3Urls[index],
                          style: const TextStyle(fontSize: 12),
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
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text(
                    'OGG/VORBIS radio:',
                    style: TextStyle(fontWeight: FontWeight.bold),
                  ),
                  DropdownButton(
                    value: oggUrlId,
                    items: List.generate(oggUrls.length, (index) {
                      return DropdownMenuItem(
                        value: index,
                        child: Text(
                          oggUrls[index],
                          style: const TextStyle(fontSize: 12),
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
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text(
                    'OPUS radio:',
                    style: TextStyle(fontWeight: FontWeight.bold),
                  ),
                  DropdownButton(
                    value: opusUrlId,
                    items: List.generate(opusUrls.length, (index) {
                      return DropdownMenuItem(
                        value: index,
                        child: Text(
                          opusUrls[index],
                          style: const TextStyle(fontSize: 12),
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
      ),
    );
  }
}

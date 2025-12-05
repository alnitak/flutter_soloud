import 'dart:async';
import 'dart:developer' as dev;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/buffer_widget.dart';
import 'package:flutter_soloud_example/buffer_stream/ui/seek_bar.dart';
import 'package:http/http.dart' as http;
import 'package:logging/logging.dart';

/// Example of how to play a web radio stream.
/// Please read the comments in the code to have clear understanding of
/// how to make the http request and how to set the icy-metaint for the stream.
///
/// On the web, please run without web security:
/// flutter run -d chrome --web-browser-flag "--disable-web-security"

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
  static const urls = [
    {'FLAC': 'http://stream.radioparadise.com/mellow-flacm'},
    {'FLAC': 'https://stream.radioparadise.com/rock-flac'},
    {'FLAC': 'http://s2.audiostream.hu:8091/bdpstrock_FLAC'},
    {'FLAC': 'https://mscp4.live-streams.nl:8142/lounge.ogg'},
    {'FLAC': 'https://frequence3.net-radio.fr/frequence3gold.flac'},
    // https://fmstream.org/index.php
    // 90s
    {'MP3': 'https://streams.90s90s.de/pop/mp3-128'},
    // 80s
    {'MP3': 'https://streams.80s80s.de/web/mp3-128'},
    {'MP3': 'https://p8.p4groupaudio.com/P08_MM'},
    {'MP3': 'https://frontend.streamonkey.net/nostalgie-80er/stream/mp3'},

    // https://dir.xiph.org/codecs
    {'Vorbis': 'http://play.global.audio/nova.ogg'},
    {'Vorbis': 'http://superaudio.radio.br:8074/stream'},
    {'Vorbis': 'http://stream.lazaradio.com:8100/live.ogg'},
    {'Vorbis': 'http://stream.trendyradio.pl:8000/m'},
    {'Vorbis': 'http://play.global.audio/nrj.ogg'},
    {'Vorbis': 'http://play.global.audio/radio1rock.ogg'},
    {'Opus': 'http://radio.glafir.ru:7000/pop-mix'},
    {'Opus': 'http://icecast.err.ee/klarajazz.opus'},
    {'Opus': 'http://xfer.hirschmilch.de:8000/prog-house.opus'},
    {'Opus': 'http://icecast.walmradio.com:8000/otr_opus'},
    {'Opus': 'http://xfer.hirschmilch.de:8000/techno.opus'},
    {'Opus': 'http://radio.glafir.ru:7000/classic'},
  ];

  static const bufferingType = BufferingType.released;

  int urlId = 0;
  AudioSource? source;
  final streamBuffering = ValueNotifier(false);
  StreamSubscription<List<int>>? subscription;
  final urlController = TextEditingController(text: '');
  final connectionError = ValueNotifier<String>('');
  final connectionInfo = TextEditingController(text: '');
  final metadataText = TextEditingController(text: '');
  http.Client? client;
  http.StreamedResponse? currentStream;
  bool mp3IcyMetaIntSent = false;

  void parseConnectionInfo(Map<String, String> headers) {
    final info = StringBuffer();

    /// Build a string with all headers on each line starting with `icy-`
    for (final key in headers.keys) {
      if (key.startsWith('icy-')) {
        info.writeln('$key: ${headers[key]}');
      }
    }
    connectionInfo.text = info.toString();
  }

  Future<void> resetConnections() async {
    // Cancel any existing connection
    await subscription?.cancel();
    subscription = null;
    currentStream = null;
    client?.close();
    client = null;
  }

  Future<void> connectToUrl(String url) async {
    urlController.text = url;
    try {
      await resetConnections();

      // Create a new client and request
      client = http.Client();
      final request = http.Request('GET', Uri.parse(url));

      /// MP3 streams require the Icy-MetaData header to get back the position
      /// of the metadata in the packets received.
      /// In the `currentStream.headers` map, you will get this value
      /// in the `icy-metaint` key. This value should be told to flutter_soloud
      /// using `SoLoud.setMp3BufferIcyMetaInt(value)` before any call
      /// to `SoLoud.addAudioDataStream`.
      /// Aside the `icy-metaint` value, you will also have othe `icy-*' values
      /// to consider for the metadata available for all stream formats.
      request.headers.addAll({'Icy-MetaData': '1'});
      currentStream = await client!.send(request);
      parseConnectionInfo(currentStream!.headers);
      mp3IcyMetaIntSent = false;

      // Handle redirections
      if (currentStream!.statusCode == 301 ||
          currentStream!.statusCode == 302) {
        final redirectUrl = currentStream!.headers['location'];
        if (redirectUrl != null) {
          // Close current connection and try with new URL
          await resetConnections();
          await connectToUrl(redirectUrl);
          return;
        }
      }

      // Check for successful connection
      if (currentStream!.statusCode != 200) {
        connectionError.value = 'error: ${currentStream!.statusCode} - '
            '${currentStream!.reasonPhrase}';

        await resetConnections();
        return;
      }

      // Listen to the stream and feed data to the audio source
      subscription = currentStream!.stream.listen(
        (data) {
          connectionError.value = '';
          if (!mp3IcyMetaIntSent) {
            mp3IcyMetaIntSent = true;
            // set it when receiving the first audio chunk
            SoLoud.instance.setBufferIcyMetaInt(
              source!,
              int.parse(currentStream!.headers['icy-metaint'] ?? '0'),
            );
          }
          if (source != null) {
            try {
              SoLoud.instance
                  .addAudioDataStream(source!, Uint8List.fromList(data));
            } on SoLoudStreamEndedAlreadyCppException catch (e) {
              debugPrint('The buffer has been filled: $e');
              resetConnections();
            } catch (e) {
              debugPrint('Error adding audio data: $e');
            }
          }
        },
        onError: (Object error) async {
          connectionError.value = 'Stream error: $error';
          debugPrint('Stream error: $error');
          await resetConnections();
        },
        onDone: () {
          debugPrint('Stream closed: done');
        },
      );
    } catch (e) {
      connectionError.value = 'Connection error: $e';
      debugPrint('Connection error: $e');
      await resetConnections();
      rethrow;
    }
  }

  Future<void> playUrl(String url) async {
    metadataText.text = '';
    connectionError.value = '';
    await resetConnections();
    await SoLoud.instance.disposeAllSources();

    streamBuffering.value = true;
    source = SoLoud.instance.setBufferStream(
      maxBufferSizeBytes: 1024 * 1024 * 200, // 100 MB
      bufferingTimeNeeds: 3,
      format: BufferType.auto,
      bufferingType: bufferingType,
      channels: Channels.stereo,
      onBuffering: (isBuffering, handle, time) async {
        // debugPrint('started buffering? $isBuffering  with '
        //     'handle: $handle at time $time');
        streamBuffering.value = isBuffering;
      },
      onMetadata: (metadata) {
        debugPrint(metadata.toString());
        metadataText.text = metadata.toString();
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
        child: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            spacing: 20,
            children: [
              TextField(
                controller: urlController,
                decoration: const InputDecoration(
                  labelText: 'Choose below or enter a URL to stream',
                  hintText: 'e.g. http://example.com/stream.mp3',
                ),
                onSubmitted: (value) {
                  if (value.isNotEmpty) {
                    playUrl(value);
                  }
                  setState(() {});
                },
              ),
              Column(
                mainAxisSize: MainAxisSize.min,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  const Text(
                    'Choose icecast radio:  ',
                    style: TextStyle(fontWeight: FontWeight.bold),
                  ),
                  DropdownButton(
                    value: urlId,
                    isDense: true,
                    items: List.generate(urls.length, (index) {
                      return DropdownMenuItem(
                        value: index,
                        child: Text(
                          '${urls[index].keys.first} - '
                          '${urls[index].values.first}',
                          style: const TextStyle(fontSize: 12),
                        ),
                      );
                    }),
                    onChanged: (value) {
                      urlId = value ?? 0;
                      playUrl(urls[urlId].values.first);
                      if (context.mounted) {
                        setState(() {});
                      }
                    },
                  ),
                ],
              ),
              OutlinedButton(
                onPressed: () async {
                  await SoLoud.instance.disposeAllSources();
                  connectionError.value = 'STOPPED';
                  await subscription?.cancel();
                  subscription = null;
                  currentStream = null;
                  client?.close();
                  client = null;
                },
                child: const Text('stop'),
              ),

              /// Only with preserved buffering is possible to seek
              if (bufferingType == BufferingType.preserved)
                SeekBar(source: source),

              ValueListenableBuilder(
                valueListenable: streamBuffering,
                builder: (context, value, child) {
                  return Row(
                    crossAxisAlignment: CrossAxisAlignment.end,
                    spacing: 16,
                    children: [
                      BufferBar(
                        bufferingType: bufferingType,
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
              TextField(
                controller: connectionInfo,
                readOnly: true,
                minLines: 3,
                maxLines: 10,
                style: const TextStyle(fontFamily: 'monospace', fontSize: 11),
                decoration: const InputDecoration(
                  border: OutlineInputBorder(),
                  labelText: 'Connection info',
                ),
              ),
              TextField(
                controller: metadataText,
                readOnly: true,
                minLines: 3,
                maxLines: 10,
                style: const TextStyle(fontFamily: 'monospace', fontSize: 11),
                decoration: const InputDecoration(
                  border: OutlineInputBorder(),
                  labelText: 'metadata',
                ),
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

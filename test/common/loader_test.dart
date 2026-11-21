// SoLoudLoader (lib/src/utils/loader_io.dart): stages assets and URLs into
// temporary files that SoLoud then loads. URLs are served by a local
// dart:io HttpServer; path_provider is mocked to a fresh temp directory.
// loadUrl/loadAsset end in SoLoud.loadFile, so the engine is started.
@TestOn('vm')
@Tags(['device'])
library;

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/utils/loader_io.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;
import 'package:http/testing.dart';
import 'package:path/path.dart' as p;

/// A mono 16-bit WAV; [tone] makes payloads of different files differ.
Uint8List _wav({int frames = 2205, int tone = 1}) {
  final data = frames * 2;
  final b = ByteData(44 + data);
  void str(int o, String s) {
    for (var i = 0; i < s.length; i++) {
      b.setUint8(o + i, s.codeUnitAt(i));
    }
  }

  str(0, 'RIFF');
  b.setUint32(4, 36 + data, Endian.little);
  str(8, 'WAVE');
  str(12, 'fmt ');
  b
    ..setUint32(16, 16, Endian.little)
    ..setUint16(20, 1, Endian.little)
    ..setUint16(22, 1, Endian.little)
    ..setUint32(24, 22050, Endian.little)
    ..setUint32(28, 44100, Endian.little)
    ..setUint16(32, 2, Endian.little)
    ..setUint16(34, 16, Endian.little);
  str(36, 'data');
  b.setUint32(40, data, Endian.little);
  for (var i = 0; i < frames; i++) {
    b.setInt16(44 + i * 2, (i * tone) % 2000 - 1000, Endian.little);
  }
  return b.buffer.asUint8List();
}

/// An AssetBundle that serves [assets] and counts loads.
class _FakeBundle extends CachingAssetBundle {
  _FakeBundle(this.assets);

  final Map<String, Uint8List> assets;
  final loads = <String>[];

  @override
  Future<ByteData> load(String key) async {
    loads.add(key);
    final bytes = assets[key];
    if (bytes == null) throw FlutterError('Unable to load asset: "$key".');
    return ByteData.sublistView(bytes);
  }
}

/// Local HTTP server: `/ok/<n>.wav` serves a WAV, `/garbage` 200 with bytes
/// that are not audio, `/status/<code>` that status, anything else 404.
class _Server {
  _Server._(this._server) {
    _server.listen((request) async {
      final path = request.uri.path;
      hits.add(path);
      final response = request.response;
      if (path.startsWith('/ok/')) {
        response
          ..headers.contentType = ContentType('audio', 'wav')
          ..add(_wav(tone: path.hashCode % 7 + 1));
      } else if (path == '/garbage') {
        response.add(Uint8List.fromList(List.filled(4096, 0x5a)));
      } else if (path.startsWith('/status/')) {
        response.statusCode = int.parse(path.split('/').last);
      } else {
        response.statusCode = HttpStatus.notFound;
      }
      await response.close();
    });
  }

  static Future<_Server> start() async =>
      _Server._(await HttpServer.bind(InternetAddress.loopbackIPv4, 0));

  final HttpServer _server;
  final hits = <String>[];

  String url(String path) => 'http://127.0.0.1:${_server.port}$path';

  Future<void> close() => _server.close(force: true);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory temp;
  late _Server server;
  late SoLoudLoader loader;

  Directory loaderDir() =>
      Directory(p.join(temp.path, 'SoLoudLoader-Temp-Files'));

  setUpAll(() async {
    // flutter_test answers every HttpClient request with 400 by default.
    HttpOverrides.global = null;
    temp = Directory.systemTemp.createTempSync('soloud_loader');
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel('plugins.flutter.io/path_provider'),
          (call) async => temp.path,
        );
    server = await _Server.start();
    await SoLoud.instance.init();
  });

  tearDownAll(() async {
    SoLoud.instance.deinit();
    await server.close();
    temp.deleteSync(recursive: true);
  });

  setUp(() {
    loader = SoLoudLoader();
    server.hits.clear();
  });

  tearDown(() async {
    await SoLoud.instance.disposeAllSources();
    await loader.cleanUp();
  });

  group('before initialize()', () {
    test('is not initialized', () {
      expect(loader.isInitialized, isFalse);
    });

    test('loadUrl and loadAsset throw a temporary-folder exception', () async {
      await expectLater(
        loader.loadUrl(server.url('/ok/a.wav'), LoadMode.memory),
        throwsA(isA<SoLoudTemporaryFolderFailedException>()),
      );
      await expectLater(
        loader.loadAsset(
          'a.wav',
          LoadMode.memory,
          assetBundle: _FakeBundle({'a.wav': _wav()}),
        ),
        throwsA(isA<SoLoudTemporaryFolderFailedException>()),
      );
      expect(server.hits, isEmpty, reason: 'nothing is downloaded');
    });

    test('cleanUp is a harmless no-op', () async {
      await loader.cleanUp();
    });
  });

  group('initialize()', () {
    test('creates the temp directory under path_provider', () async {
      await loader.initialize();
      expect(loader.isInitialized, isTrue);
      expect(loaderDir().existsSync(), isTrue);
    });

    test('can run several times', () async {
      await loader.initialize();
      await loader.initialize();
      expect(loader.isInitialized, isTrue);
    });
  });

  group('loadUrl', () {
    setUp(() => loader.initialize());

    test('downloads into a temp file and loads it', () async {
      final url = server.url('/ok/one.wav');
      final source = await loader.loadUrl(url, LoadMode.memory);
      expect(source.soundHash.isValid, isTrue);
      expect(SoLoud.instance.activeSounds, contains(source));
      expect(SoLoud.instance.getLength(source), greaterThan(Duration.zero));

      final file = File(source.tempFilePath);
      expect(p.isWithin(loaderDir().path, file.path), isTrue);
      expect(
        file.readAsBytesSync(),
        _wav(tone: '/ok/one.wav'.hashCode % 7 + 1),
      );
      expect(server.hits, ['/ok/one.wav']);
    });

    test('works with LoadMode.disk and autoDispose', () async {
      final source = await loader.loadUrl(
        server.url('/ok/disk.wav'),
        LoadMode.disk,
        autoDispose: true,
      );
      expect(source.autoDispose, isTrue);
      expect(SoLoud.instance.getLength(source), greaterThan(Duration.zero));
    });

    test('a second load of the same URL reuses the temp file', () async {
      final url = server.url('/ok/cached.wav');
      final first = await loader.loadUrl(url, LoadMode.memory);
      final second = await loader.loadUrl(url, LoadMode.memory);
      expect(server.hits, ['/ok/cached.wav'], reason: 'downloaded once');
      expect(second.tempFilePath, first.tempFilePath);
      expect(second.soundHash.isValid, isTrue);
    });

    test('a deleted cached file is downloaded again', () async {
      final url = server.url('/ok/deleted.wav');
      final first = await loader.loadUrl(url, LoadMode.memory);
      File(first.tempFilePath).deleteSync();
      final second = await loader.loadUrl(url, LoadMode.memory);
      expect(server.hits, hasLength(2));
      expect(File(second.tempFilePath).existsSync(), isTrue);
    });

    test('different URLs get different temp files', () async {
      final a = await loader.loadUrl(server.url('/ok/a.wav'), LoadMode.memory);
      final b = await loader.loadUrl(server.url('/ok/b.wav'), LoadMode.memory);
      expect(a.tempFilePath, isNot(b.tempFilePath));
      expect(a.soundHash, isNot(b.soundHash));
    });

    test('the cache is per loader', () async {
      final url = server.url('/ok/shared.wav');
      await loader.loadUrl(url, LoadMode.memory);
      final other = SoLoudLoader();
      await other.initialize();
      await other.loadUrl(url, LoadMode.memory);
      expect(server.hits, hasLength(2));
    });

    test('a 404 throws SoLoudNetworkStatusCodeException', () async {
      await expectLater(
        loader.loadUrl(server.url('/missing.wav'), LoadMode.memory),
        throwsA(
          isA<SoLoudNetworkStatusCodeException>().having(
            (e) => e.statusCode,
            'statusCode',
            404,
          ),
        ),
      );
      expect(loaderDir().listSync(), isEmpty, reason: 'no file is left');
    });

    test('any non-200 status throws, with its code', () async {
      for (final code in [204, 403, 500, 503]) {
        await expectLater(
          loader.loadUrl(server.url('/status/$code'), LoadMode.memory),
          throwsA(
            isA<SoLoudNetworkStatusCodeException>().having(
              (e) => e.statusCode,
              'statusCode',
              code,
            ),
          ),
          reason: 'status $code',
        );
      }
    });

    test(
      'a failure is not cached: the next attempt hits the network',
      () async {
        final url = server.url('/status/500');
        for (var i = 0; i < 2; i++) {
          await expectLater(
            loader.loadUrl(url, LoadMode.memory),
            throwsA(isA<SoLoudNetworkStatusCodeException>()),
          );
        }
        expect(server.hits, hasLength(2));
      },
    );

    test('a malformed URL throws FormatException', () async {
      await expectLater(
        loader.loadUrl('http://[::1', LoadMode.memory),
        throwsFormatException,
      );
    });

    test('an unreachable host throws a client exception', () async {
      // Port 1 on loopback refuses the connection.
      await expectLater(
        loader.loadUrl('http://127.0.0.1:1/x.wav', LoadMode.memory),
        throwsA(isA<http.ClientException>()),
      );
    });

    test('undecodable bytes throw and the temp file is removed', () async {
      await expectLater(
        loader.loadUrl(server.url('/garbage'), LoadMode.memory),
        throwsA(isA<SoLoudCppException>()),
      );
      expect(server.hits, ['/garbage']);
      expect(loaderDir().listSync(), isEmpty);
    });

    test('uses the given httpClient', () async {
      final requests = <Uri>[];
      final client = MockClient((request) async {
        requests.add(request.url);
        return http.Response.bytes(_wav(tone: 3), 200);
      });
      const url = 'http://example.invalid/mock.wav';
      final source = await loader.loadUrl(
        url,
        LoadMode.memory,
        httpClient: client,
      );
      expect(requests, [Uri.parse(url)]);
      expect(File(source.tempFilePath).readAsBytesSync(), _wav(tone: 3));
      expect(server.hits, isEmpty);
    });

    test('throws SoLoudNotInitializedException without the engine and '
        'leaves no temp file', () async {
      SoLoud.instance.deinit();
      addTearDown(SoLoud.instance.init);
      await expectLater(
        loader.loadUrl(server.url('/ok/noengine.wav'), LoadMode.memory),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
      expect(loaderDir().listSync(), isEmpty);
    });
  });

  group('loadAsset', () {
    setUp(() => loader.initialize());

    test('stages the asset and caches it', () async {
      final bundle = _FakeBundle({'sounds/a.wav': _wav(tone: 5)});
      final first = await loader.loadAsset(
        'sounds/a.wav',
        LoadMode.memory,
        assetBundle: bundle,
      );
      final second = await loader.loadAsset(
        'sounds/a.wav',
        LoadMode.memory,
        assetBundle: bundle,
      );
      expect(bundle.loads, ['sounds/a.wav'], reason: 'read once');
      expect(second.tempFilePath, first.tempFilePath);
      expect(File(first.tempFilePath).readAsBytesSync(), _wav(tone: 5));
    });

    test('an asset and a URL with the same key do not collide', () async {
      final url = server.url('/ok/same.wav');
      final fromUrl = await loader.loadUrl(url, LoadMode.memory);
      final fromAsset = await loader.loadAsset(
        url,
        LoadMode.memory,
        assetBundle: _FakeBundle({url: _wav(tone: 6)}),
      );
      expect(fromAsset.tempFilePath, isNot(fromUrl.tempFilePath));
    });

    test('a missing asset rethrows the bundle error', () async {
      await expectLater(
        loader.loadAsset(
          'nope.wav',
          LoadMode.memory,
          assetBundle: _FakeBundle({}),
        ),
        throwsA(isA<FlutterError>()),
      );
      expect(loaderDir().listSync(), isEmpty);
    });

    test('an undecodable asset throws and removes its temp file', () async {
      await expectLater(
        loader.loadAsset(
          'bad.wav',
          LoadMode.memory,
          assetBundle: _FakeBundle({'bad.wav': Uint8List(1000)}),
        ),
        throwsA(isA<SoLoudCppException>()),
      );
      expect(loaderDir().listSync(), isEmpty);
    });
  });

  group('cleanUp and automaticCleanup', () {
    test('cleanUp deletes the temp files and forgets the cache', () async {
      await loader.initialize();
      final url = server.url('/ok/clean.wav');
      final source = await loader.loadUrl(url, LoadMode.memory);
      expect(File(source.tempFilePath).existsSync(), isTrue);

      await loader.cleanUp();
      expect(loaderDir().listSync(), isEmpty);
      expect(loaderDir().existsSync(), isTrue, reason: 'only the contents');

      await loader.loadUrl(url, LoadMode.memory);
      expect(server.hits, hasLength(2), reason: 'downloaded again');
    });

    test('automaticCleanup wipes the directory on initialize()', () async {
      await loader.initialize();
      await loader.loadUrl(server.url('/ok/auto.wav'), LoadMode.memory);
      final stray = File(p.join(loaderDir().path, 'stray'))
        ..writeAsStringSync('x');
      expect(loaderDir().listSync(), hasLength(2));

      loader.automaticCleanup = true;
      await loader.initialize();
      expect(stray.existsSync(), isFalse);
      expect(loaderDir().listSync(), isEmpty);
    });

    test('without automaticCleanup, initialize() keeps the files', () async {
      await loader.initialize();
      final source = await loader.loadUrl(
        server.url('/ok/keep.wav'),
        LoadMode.memory,
      );
      await loader.initialize();
      expect(File(source.tempFilePath).existsSync(), isTrue);
    });

    test('a fresh loader with automaticCleanup also wipes leftovers', () async {
      await loader.initialize();
      await loader.loadUrl(server.url('/ok/left.wav'), LoadMode.memory);
      final fresh = SoLoudLoader()..automaticCleanup = true;
      await fresh.initialize();
      expect(loaderDir().listSync(), isEmpty);
    });
  });

  test('SoLoud.loadUrl goes through the loader and keeps the URL', () async {
    final url = server.url('/ok/public.wav');
    final source = await SoLoud.instance.loadUrl(url);
    expect(source.soundPath, url);
    expect(p.isWithin(loaderDir().path, source.tempFilePath), isTrue);
  });
}

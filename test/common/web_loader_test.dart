// The web loader (lib/src/utils/loader_web.dart) against a real local HTTP
// server (test/support/web_hybrid/loader_server.dart, started from the
// browser with spawnHybridUri) and the real WASM engine: loadUrl with a 404,
// a response the browser refuses (no CORS header), a connection dropped
// mid-download, an empty body, bytes that are no audio; loadAsset with a
// missing asset. Each failure must throw a typed exception, leave no sound
// behind, and leave the engine working.
//
// Browser only: run with ./test/support/web_tests.sh test/common/web_loader_test.dart
// (add --wasm for the multi-threaded build).
@TestOn('browser')
@Tags(['device'])
library;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:http/http.dart' as http;

import '../support/sounds.dart';

void main() {
  setUpEngineTests(globalVolume: 0);
  final soloud = SoLoud.instance;
  late String base;

  setUpAll(() async {
    final channel = spawnHybridUri('../support/web_hybrid/loader_server.dart');
    final port = (await channel.stream.first as num).toInt();
    base = 'http://127.0.0.1:$port';
  });

  /// Loading [path] must fail with [matcher], leave no sound behind, and the
  /// engine must still load and play.
  Future<void> expectCleanFailure(String path, Matcher matcher) async {
    final before = soloud.activeSounds.length;
    await expectLater(soloud.loadUrl('$base$path'), throwsA(matcher));
    expect(soloud.activeSounds.length, before, reason: 'no sound left behind');
    final ok = await soloud.loadUrl('$base/ok.wav');
    final h = soloud.play(ok);
    expect(soloud.getIsValidVoiceHandle(h), isTrue);
    await soloud.disposeSource(ok);
  }

  test('a valid WAV loads and plays', () async {
    final s = await soloud.loadUrl('$base/ok.wav');
    expect(s.soundPath, '$base/ok.wav');
    expect(soloud.getLength(s).inMilliseconds, closeTo(1000, 5));
    final h = soloud.play(s);
    await Future<void>.delayed(const Duration(milliseconds: 200));
    expect(soloud.getPosition(h), greaterThan(Duration.zero));
  });

  test('404 throws SoLoudNetworkStatusCodeException', () async {
    await expectCleanFailure(
      '/missing',
      isA<SoLoudNetworkStatusCodeException>().having(
        (e) => e.statusCode,
        'statusCode',
        404,
      ),
    );
  });

  test(
    'a response the browser refuses (CORS) throws ClientException',
    () async {
      await expectCleanFailure('/nocors.wav', isA<http.ClientException>());
    },
  );

  test('a connection dropped mid-download throws, nothing is loaded', () async {
    await expectCleanFailure('/cut.wav', isA<http.ClientException>());
  });

  test('an empty body throws a SoLoud exception', () async {
    await expectCleanFailure('/empty.wav', isA<SoLoudException>());
  });

  test('bytes that are no audio throw a SoLoud exception', () async {
    await expectCleanFailure('/garbage.wav', isA<SoLoudException>());
  });

  test('an invalid URL throws FormatException', () async {
    await expectLater(soloud.loadUrl('http://[::1'), throwsFormatException);
  });

  test(
    'deinit during a download throws SoLoudNotInitializedException',
    () async {
      final future = soloud.loadUrl('$base/slow.wav');
      await Future<void>.delayed(const Duration(milliseconds: 200));
      soloud.deinit();
      await expectLater(future, throwsA(isA<SoLoudNotInitializedException>()));
    },
  );

  // rootBundle.load of a missing asset never completes on the flutter test
  // web page, so a bundle that throws FlutterError, as for a missing asset,
  // stands in for it.
  test('loadAsset with a missing asset throws, nothing is loaded', () async {
    final before = soloud.activeSounds.length;
    await expectLater(
      soloud.loadAsset('assets/missing.wav', assetBundle: _MissingBundle()),
      throwsA(isA<FlutterError>()),
    );
    expect(soloud.activeSounds.length, before);
    final tone = await loadTestWaveform();
    expect(soloud.getIsValidVoiceHandle(soloud.play(tone)), isTrue);
  });

  test(
    'loadAsset from a custom bundle that fails rethrows its error',
    () async {
      await expectLater(
        soloud.loadAsset('a.wav', assetBundle: _FailingBundle()),
        throwsA(isA<StateError>()),
      );
    },
  );
}

class _FailingBundle extends CachingAssetBundle {
  @override
  Future<ByteData> load(String key) => Future.error(StateError('no $key'));
}

class _MissingBundle extends CachingAssetBundle {
  @override
  Future<ByteData> load(String key) =>
      Future.error(FlutterError('Unable to load asset: "$key".'));
}

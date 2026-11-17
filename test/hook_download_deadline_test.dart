// The build hook's HttpClient download (hook/build.dart), the fallback for
// when curl is unavailable or failed, against a local HTTP server.
//
// curl gets -m 60 for the whole transfer. The fallback had timeouts for the
// connection and the response headers only, so a transfer that stalled after
// the headers hung the build for good.
@TestOn('vm')
library;

import 'dart:async';
import 'dart:io';
import 'dart:math';

import 'package:test/test.dart';

import '../hook/build.dart' as hook;

void main() {
  late Directory scratch;
  final random = Random(42);

  setUp(() {
    scratch = Directory.systemTemp.createTempSync('soloud_hook_test_');
  });

  tearDown(() {
    scratch.deleteSync(recursive: true);
  });

  List<int> randomBytes(int length) =>
      List<int>.generate(length, (_) => random.nextInt(256));

  test('downloads a complete transfer', () async {
    final body = randomBytes(4096);
    final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
    addTearDown(() => server.close(force: true));
    server.listen((request) async {
      request.response.add(body);
      await request.response.close();
    });
    final file = File('${scratch.path}/download.tar.gz');

    await hook.XiphLink.debugDownloadWithHttpClient(
      'http://127.0.0.1:${server.port}/archive.tar.gz',
      file,
    );
    expect(file.readAsBytesSync(), body);
  });

  test(
    'a transfer that stalls after the headers fails at the deadline',
    () async {
      final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
      addTearDown(() => server.close(force: true));
      var requests = 0;
      server.listen((request) async {
        requests++;
        final response = request.response
          ..contentLength = 1 << 20
          ..add(randomBytes(1024));
        // The rest of the body never comes.
        await response.flush();
      });
      final file = File('${scratch.path}/download.tar.gz');

      final watch = Stopwatch()..start();
      await expectLater(
        hook.XiphLink.debugDownloadWithHttpClient(
          'http://127.0.0.1:${server.port}/archive.tar.gz',
          file,
          deadline: const Duration(seconds: 2),
        ),
        throwsA(isA<TimeoutException>()),
      );
      expect(watch.elapsed, lessThan(const Duration(seconds: 15)));
      expect(requests, 1);
    },
    timeout: const Timeout(Duration(seconds: 30)),
  );
}

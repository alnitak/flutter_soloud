// A local HTTP server for the browser loader test
// (test/common/web_loader_test.dart), started from the browser with
// spawnHybridUri(): it runs in the VM, next to the test runner, and sends its
// port over the channel. Endpoints:
//   /ok.wav       a valid WAV, CORS allowed
//   /missing      404, CORS allowed
//   /nocors.wav   a valid WAV without Access-Control-Allow-Origin (the
//                 browser must refuse it)
//   /cut.wav      announces a WAV's full length, sends half and drops the
//                 connection (network lost mid-download)
//   /empty.wav    200 with an empty body, CORS allowed
//   /garbage.wav  200 with bytes that are no audio, CORS allowed
//   /slow.wav     a valid WAV after a 1.5 s delay, CORS allowed
import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

// ignore: depend_on_referenced_packages, a dependency of package:test
import 'package:stream_channel/stream_channel.dart';

Uint8List _wav({int frames = 22050}) {
  final data = frames * 2;
  final b = ByteData(44 + data);
  void str(int o, String s) {
    for (var i = 0; i < s.length; i++) {
      b.setUint8(o + i, s.codeUnitAt(i));
    }
  }

  str(0, 'RIFF');
  b.setUint32(4, 36 + data, Endian.little);
  str(8, 'WAVEfmt ');
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
    b.setInt16(44 + i * 2, (i % 50 < 25) ? 8000 : -8000, Endian.little);
  }
  return b.buffer.asUint8List();
}

Future<void> hybridMain(StreamChannel<Object?> channel) async {
  final server = await HttpServer.bind(InternetAddress.loopbackIPv4, 0);
  final wav = _wav();
  server.listen((req) async {
    final res = req.response;
    void cors() => res.headers.set('Access-Control-Allow-Origin', '*');
    switch (req.uri.path) {
      case '/ok.wav':
        cors();
        res.headers.contentType = ContentType('audio', 'wav');
        res.add(wav);
        await res.close();
      case '/missing':
        cors();
        res.statusCode = 404;
        await res.close();
      case '/nocors.wav':
        res.add(wav);
        await res.close();
      case '/cut.wav':
        // Raw HTTP, so the connection can be dropped mid-body.
        final socket = await res.detachSocket(writeHeaders: false);
        socket
          ..write(
            'HTTP/1.1 200 OK\r\n'
            'Access-Control-Allow-Origin: *\r\n'
            'Content-Type: audio/wav\r\n'
            'Content-Length: ${wav.length}\r\n\r\n',
          )
          ..add(wav.sublist(0, wav.length ~/ 2));
        await socket.flush();
        await Future<void>.delayed(const Duration(milliseconds: 100));
        socket.destroy();
      case '/empty.wav':
        cors();
        await res.close();
      case '/garbage.wav':
        cors();
        res.add(List<int>.generate(4096, (i) => (i * 7919) & 0xff));
        await res.close();
      case '/slow.wav':
        cors();
        await Future<void>.delayed(const Duration(milliseconds: 1500));
        res.add(wav);
        await res.close();
      default:
        res.statusCode = 404;
        await res.close();
    }
  });
  channel.sink.add(server.port);
  // Runs until the test's channel closes.
  await channel.stream.drain<void>();
  await server.close(force: true);
}

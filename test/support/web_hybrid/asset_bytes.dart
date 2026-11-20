// Sends the bytes of the requested files under example/assets/audio/ to a
// browser test (spawnHybridUri, runs in the VM): the test sends a list of
// file names, this answers with a map name -> bytes (null when missing).
// Used by test/common/web_decoders_test.dart.
import 'dart:io';

// ignore: depend_on_referenced_packages, a dependency of package:test
import 'package:stream_channel/stream_channel.dart';

Future<void> hybridMain(StreamChannel<Object?> channel) async {
  await for (final message in channel.stream) {
    final names = (message! as List).cast<String>();
    final out = <String, List<int>?>{};
    for (final n in names) {
      final f = File('example/assets/audio/$n');
      out[n] = f.existsSync() ? f.readAsBytesSync() : null;
    }
    channel.sink.add(out);
  }
}

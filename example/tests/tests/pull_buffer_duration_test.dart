import 'dart:async';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Fast duration-probe check for pull-buffer streams.
///
/// Feeds the file in chunks until the Ogg tail probe reports a duration, then
/// compares it to the reference duration from `loadMem`.
Future<OutputBuffer> testPullBufferDuration(String assetName) async {
  final strBuf = OutputBuffer();
  await initialize();

  final bytes =
      (await rootBundle.load('assets/audio/$assetName')).buffer.asUint8List();

  final refSource = await SoLoud.instance.loadMem('ref', bytes);
  final refDuration = SoLoud.instance.getLength(refSource);
  await SoLoud.instance.disposeSource(refSource);

  const chunkSize = 1024;
  final durationCompleter = Completer<double>();
  final fetchedOffsets = <int>{};
  var durationReceived = false;
  late final AudioSource source;

  source = SoLoud.instance.setPullBufferStream(
    bufferSizeBytes: 1024 * 100 * 1,
    bufferTriggerPosition: 2 / 3,
    audioSizeBytes: bytes.length,
    onAudioDuration: (duration) {
      strBuf.writeln('audioDuration=${(duration * 1000).round()}ms');
      if (!durationCompleter.isCompleted) {
        durationCompleter.complete(duration);
      }
    },
    onMoreDataIsNeeded: (offset) {
      if (durationReceived || offset < 0 || offset >= bytes.length) {
        return;
      }
      if (!fetchedOffsets.add(offset)) {
        return;
      }
      final end = (offset + chunkSize).clamp(0, bytes.length);
      final chunk = bytes.sublist(offset, end);
      SoLoud.instance.addPullBufferDataStream(
        source,
        chunk,
        offset: offset,
      );
    },
  );

  final duration = await durationCompleter.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () => -1.0,
  );
  durationReceived = true;
  strBuf.writeln('duration reported: ${(duration * 1000).round()}ms');
  assert(
    duration > 0.0,
    'Expected a positive duration for $assetName',
  );
  assert(
    (duration * 1000 - refDuration.inMilliseconds).abs() <
        refDuration.inMilliseconds * 0.1 + 1000,
    'Reported duration ${(duration * 1000).round()}ms differs from reference '
    '${refDuration.inMilliseconds}ms for $assetName',
  );

  deinit();
  return strBuf;
}

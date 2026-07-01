import 'dart:async';
import 'dart:math';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Generate a short sine-wave PCM buffer (mono, s16le).
Uint8List _generatePcmData({
  int sampleRate = 24000,
  double durationSeconds = 1.0,
  double frequency = 440,
}) {
  final sampleCount = (sampleRate * durationSeconds).toInt();
  final bytes = ByteData(sampleCount * 2); // 16-bit samples = 2 bytes each
  for (var i = 0; i < sampleCount; i++) {
    final sample =
        (sin(2 * pi * frequency * i / sampleRate) * 16000).toInt().clamp(
              -32768,
              32767,
            );
    bytes.setInt16(i * 2, sample, Endian.little);
  }
  return bytes.buffer.asUint8List();
}

/// Test that buffer stream callbacks survive for the lifetime of the sound
/// and are properly released on disposal.
///
/// PR #445 fixes a bug where NativeCallable wrappers for onBuffering /
/// onMetadata were dropped immediately after setBufferStream(), letting the
/// GC free the trampoline while native still held its address.
Future<OutputBuffer> testBufferStreamCallbacks() async {
  final buf = OutputBuffer();
  await initialize();
  final pcm = _generatePcmData();

  // ── 1. onBuffering callback fires ───────────────────────────────────────
  //    Play BEFORE adding data so the stream enters the buffering state,
  //    then add data so it transitions to un-buffering — both fire the
  //    onBuffering callback.
  final bufferingEvents = <bool>[];
  final unbufferingReceived = Completer<void>();

  final stream1 = SoLoud.instance.setBufferStream(
    bufferingTimeNeeds: 0.3,
    onBuffering: (isBuffering, handle, time) {
      bufferingEvents.add(isBuffering);
      if (!isBuffering && !unbufferingReceived.isCompleted) {
        unbufferingReceived.complete();
      }
    },
  );

  // Start playback with no data — enters buffering (paused) state.
  SoLoud.instance.play(stream1);
  await delay(100);

  // Feed data — once enough accumulates the stream un-buffers.
  SoLoud.instance.addAudioDataStream(stream1, pcm);

  await unbufferingReceived.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () {
      assert(false, 'onBuffering callback never fired');
    },
  );

  assert(bufferingEvents.isNotEmpty, 'should have received buffering events');
  buf.writeln('1. onBuffering callback fired: OK '
      '(${bufferingEvents.length} events)');

  // ── 2. Dispose the sound — no crash ─────────────────────────────────────
  await SoLoud.instance.disposeSource(stream1);
  await delay(100);
  buf.writeln('2. disposeSource after callbacks: OK');

  // ── 3. New stream after disposal — callbacks still work ─────────────────
  final secondUnbuffering = Completer<void>();

  final stream2 = SoLoud.instance.setBufferStream(
    bufferingTimeNeeds: 0.3,
    onBuffering: (isBuffering, handle, time) {
      if (!isBuffering && !secondUnbuffering.isCompleted) {
        secondUnbuffering.complete();
      }
    },
  );

  SoLoud.instance.play(stream2);
  await delay(100);
  SoLoud.instance.addAudioDataStream(stream2, pcm);

  await secondUnbuffering.future.timeout(
    const Duration(seconds: 5),
    onTimeout: () {
      assert(false, 'second onBuffering callback never fired');
    },
  );
  buf.writeln('3. New stream callbacks work after prior disposal: OK');

  // ── 4. disposeAllSources cleans up everything ───────────────────────────
  await SoLoud.instance.disposeAllSources();
  await delay(100);
  buf.writeln('4. disposeAllSources after callback streams: OK');

  deinit();

  debugPrint(buf.toString());
  return buf;
}

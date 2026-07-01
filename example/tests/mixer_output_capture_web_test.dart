import 'dart:async';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
// ignore: avoid_web_libraries_in_flutter
import 'package:flutter_soloud/src/bindings/js_extension.dart' as js;

/// Minimal web test for mixer output capture.
///
/// Run with:
///   flutter run -d chrome --wasm -t tests/mixer_output_capture_web_test.dart
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();

  await SoLoud.instance.init();

  runApp(const CaptureWebTestApp());
}

class CaptureWebTestApp extends StatefulWidget {
  const CaptureWebTestApp({super.key});

  @override
  State<CaptureWebTestApp> createState() => _CaptureWebTestAppState();
}

class _CaptureWebTestAppState extends State<CaptureWebTestApp> {
  final logs = <String>[];

  void log(String msg) {
    setState(() => logs.add(msg));
    // ignore: avoid_print
    print(msg);
  }

  @override
  void initState() {
    super.initState();
    _run();
  }

  Future<void> _run() async {
    log('=== MixerOutputCaptureWebTest starting ===');

    SoLoud.instance.setGlobalVolume(0.2);

    final sound = await SoLoud.instance.loadWaveform(
      WaveForm.square,
      false,
      1,
      0,
    );
    SoLoud.instance.play(sound, looping: true);

    log('active voices: ${SoLoud.instance.getActiveVoiceCount()}');

    await Future<void>.delayed(const Duration(milliseconds: 300));

    for (final format in MixerOutputFormat.values) {
      await _testFormat(format);
    }

    SoLoud.instance.deinit();
  }

  Future<void> _testFormat(MixerOutputFormat format) async {
    log('--- testing $format ---');
    final chunks = <Uint8List>[];
    const duration = 2000;
    final stream = SoLoud.instance.startMixerOutputStream(format: format);

    final subscription = stream.listen(
      (chunk) {
        final bytesHex =
            chunk.take(16).map((b) => b.toRadixString(16)).join(' ');
        log('$format chunk: ${chunk.length} bytes: $bytesHex');
        chunks.add(chunk);
      },
      onError: (Object e) => log('$format stream error: $e'),
      onDone: () => log('$format stream done'),
    );

    final debugTimer = Timer.periodic(const Duration(milliseconds: 200), (_) {
      log('$format available=${js.wasmGetMixerCaptureAvailableBytes()}');
    });

    await Future<void>.delayed(const Duration(milliseconds: duration));
    debugTimer.cancel();
    // Stop capture before canceling the subscription so the synchronous tail
    // flush is delivered through the stream.
    SoLoud.instance.stopMixerOutputStream();
    await subscription.cancel();

    final totalBytes = chunks.fold<int>(0, (sum, c) => sum + c.length);
    log('$format total bytes: $totalBytes chunks: ${chunks.length}');

    if (chunks.isEmpty) {
      log('FAIL $format: no chunks');
      return;
    }

    final first = chunks.first;
    final nonZero = first.any((b) => b != 0);
    log('$format first chunk non-zero: $nonZero');

    var magicOk = true;
    if (format == MixerOutputFormat.opus ||
        format == MixerOutputFormat.vorbis) {
      magicOk = first.length > 27 &&
          first[0] == 0x4f &&
          first[1] == 0x67 &&
          first[2] == 0x67 &&
          first[3] == 0x53;
      log('$format OggS magic: $magicOk');
    } else if (format == MixerOutputFormat.flac) {
      magicOk = first.length > 4 &&
          first[0] == 0x66 &&
          first[1] == 0x4c &&
          first[2] == 0x61 &&
          first[3] == 0x43;
      log('$format fLaC magic: $magicOk');
    }

    if (nonZero && magicOk) {
      log('PASS $format');
    } else {
      log('FAIL $format');
    }
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      home: Scaffold(
        body: ListView.builder(
          itemCount: logs.length,
          itemBuilder: (context, index) => Text(logs[index]),
        ),
      ),
    );
  }
}

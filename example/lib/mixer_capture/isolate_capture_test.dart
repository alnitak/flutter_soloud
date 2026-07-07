// ignore_for_file: avoid_print, experimental_member_use

import 'dart:async';
import 'dart:developer' as dev;
import 'dart:isolate';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const MaterialApp(home: IsolateCaptureTest()));
}

/// Very small test that initializes SoLoud in the main isolate and runs the
/// mixer output capture loop in a separate worker isolate.
class IsolateCaptureTest extends StatefulWidget {
  /// Creates a new isolate capture test.
  const IsolateCaptureTest({super.key});

  @override
  State<IsolateCaptureTest> createState() => _IsolateCaptureTestState();
}

class _IsolateCaptureTestState extends State<IsolateCaptureTest> {
  final _logs = <String>[];
  var _running = false;
  SendPort? _isolateSendPort;
  ReceivePort? _receivePort;
  StreamSubscription<dynamic>? _receiveSub;
  Isolate? _isolate;
  Completer<void>? _doneCompleter;

  @override
  void initState() {
    super.initState();
  }

  void _log(String line) {
    _logs.add(line);
    dev.log(line);
    print(line);
    if (mounted) setState(() {});
  }

  Future<void> _stop() async {
    _log('Main: stopping capture isolate...');
    _isolateSendPort?.send('stop');
    _isolateSendPort = null;

    if (_doneCompleter != null && !_doneCompleter!.isCompleted) {
      try {
        await _doneCompleter!.future.timeout(const Duration(seconds: 5));
      } on TimeoutException {
        _log('Main: isolate did not stop in time, killing it');
        _isolate?.kill();
      }
    }

    await _receiveSub?.cancel();
    _receivePort?.close();
    _receiveSub = null;
    _receivePort = null;
    _isolate = null;
    _doneCompleter = null;

    SoLoud.instance.deinit();
    setState(() => _running = false);
  }

  Future<void> _start() async {
    setState(() => _running = true);
    _logs.clear();

    _log('Main: initializing SoLoud...');
    await SoLoud.instance.init();

    _log('Main: loading and playing a looping waveform...');
    final sound = await SoLoud.instance.loadWaveform(
      WaveForm.square,
      false,
      1,
      0,
    );
    SoLoud.instance.play(sound, looping: true, volume: 0.5);
    setState(() {});

    _log('Main: spawning capture isolate...');
    _receivePort = ReceivePort();
    _doneCompleter = Completer<void>();
    _receiveSub = _receivePort!.listen((message) {
      if (message is SendPort) {
        _isolateSendPort = message;
        return;
      }
      if (message is String) {
        _log(message);
        if (message.contains('Isolate: done.')) {
          _doneCompleter?.complete();
        }
      }
    });

    _isolate = await Isolate.spawn(
      _captureIsolate,
      _receivePort!.sendPort,
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Isolate Capture Test')),
      body: Column(
        spacing: 16,
        children: [
          ElevatedButton(
            onPressed: _running ? null : _start,
            child: const Text('Start isolate capture'),
          ),
          ElevatedButton(
            onPressed: () {
              final stopwatch = Stopwatch()..start();
              while (stopwatch.elapsedMilliseconds < 2000) {}
            },
            child: const Text('Block main isolate for 2000ms'),
          ),
          const CircularProgressIndicator(),
          ElevatedButton(
            onPressed: _running ? _stop : null,
            child: const Text('Stop isolate capture'),
          ),
          Expanded(
            child: ListView.builder(
              itemCount: _logs.length,
              padding: const EdgeInsets.all(16),
              itemBuilder: (_, index) => Text(_logs[index]),
            ),
          ),
        ],
      ),
    );
  }
}

/// Runs inside a separate isolate. The SoLoud engine is already initialized in
/// C++ land by the main isolate, so here we just use [SoLoudIsolate.instance]
/// to listen to the capture stream without touching the main isolate's
/// callbacks, filters, or loader state.
@pragma('vm:entry-point')
Future<void> _captureIsolate(SendPort mainSendPort) async {
  final isolateReceivePort = ReceivePort();
  mainSendPort.send(isolateReceivePort.sendPort);

  void send(String message) => mainSendPort.send(message);

  StreamSubscription<Uint8List>? subscription;

  try {
    send('Isolate: starting mixer output capture via SoLoudIsolate...');

    final captureStream = SoLoudIsolate.instance.startMixerOutputStream(
      format: MixerOutputFormat.pcmS16le,
      chunkPCMFrames: 2048, // fixed PCM chunk
    );

    var chunkCount = 0;

    subscription = captureStream.listen(
      (Uint8List chunk) {
        chunkCount++;
        if (chunkCount % 5 == 0) {
          print('Isolate: chunk #$chunkCount - ${chunk.length} bytes');
        }
      },
    );

    print('Isolate: capturing...');
    isolateReceivePort.listen((message) async {
      if (message == 'stop') {
        await subscription?.cancel();
        SoLoudIsolate.instance.stopMixerOutputStream();
      }
    });

    send(
      'Isolate: done. Continue to listen to capture stream until '
      'main isolate stops us.',
    );
  } on Object catch (e, stackTrace) {
    await subscription?.cancel();
    SoLoudIsolate.instance.stopMixerOutputStream();
    send('Isolate: error: $e');
    send(stackTrace.toString());
  }
}

// Web capture messages and stops against a stand-in for the wasm module,
// which has the capture exports, a heap and the event worker but no audio.
// Run with:
//
//   flutter test --platform chrome test/web_mixer_output_packet_test.dart
@TestOn('browser')
library;

import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/bindings_player_web.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/mixer_output_stream_manager.dart';
import 'package:test/test.dart';
import 'package:web/web.dart' as web;

/// Holds packets the way the native capture does: by token, each tagged
/// with the capture that produced it, until claimed. Without packets it
/// models the shipped wasm build, which posts heap addresses instead.
class CaptureModule {
  CaptureModule({bool withPackets = true}) {
    module
      ..['HEAPU8'] = heap.buffer.asUint8List().toJS
      ..['wasmWorker'] = worker
      ..['_malloc'] = allocate.toJS
      ..['_free'] = free.toJS
      ..['getValue'] = ((int ptr, String type) => heap.getInt32(
        ptr,
        Endian.little,
      )).toJS
      ..['_createWorkerInWasm'] = (() => 1).toJS
      ..['_setMixerOutputCallback'] = ((int callback) {}).toJS
      ..['_startMixerCapture'] =
          ((int f, int r, int c, int size, int threshold, int chunk) {
            if (running) return PlayerErrors.playerAlreadyInited.value;
            running = true;
            captureId++;
            return PlayerErrors.noError.value;
          }).toJS
      ..['_stopMixerCapture'] = (() {
        running = false;
        stops++;
      }).toJS
      ..['_isMixerCaptureRunning'] = (() => running ? 1 : 0).toJS
      ..['_getMixerCaptureAvailableBytes'] = (() => 0).toJS;
    if (withPackets) {
      module
        ..['_getMixerCaptureId'] = (() => captureId).toJS
        ..['_takeMixerOutputPacket'] = ((int token, int idPtr, int lengthPtr) {
          final packet = packets.remove(token);
          if (packet == null) return 0;
          heap
            ..setInt32(idPtr, packet.id, Endian.little)
            ..setInt32(lengthPtr, packet.bytes.length, Endian.little);
          return place(packet.bytes);
        }).toJS;
    }
    globalContext['Module_soloud'] = module;
  }

  final module = JSObject();
  final worker = JSObject();
  final heap = ByteData(1 << 16);
  final allocations = <int, int>{};
  final packets = <int, ({int id, List<int> bytes})>{};
  int captureId = 0;
  bool running = false;
  int stops = 0;
  var _top = 8;
  var _nextToken = 0;

  void free(int ptr) {
    allocations.remove(ptr);
  }

  int allocate(int size) {
    final ptr = _top;
    _top += (size + 7) & ~7;
    allocations[ptr] = size;
    return ptr;
  }

  /// Copies [bytes] into the heap, as an allocation the consumer frees.
  int place(List<int> bytes) {
    final ptr = allocate(bytes.length);
    heap.buffer.asUint8List().setAll(ptr, bytes);
    return ptr;
  }

  /// A packet of the current capture, as the notification creates it.
  int queue(List<int> bytes) {
    packets[++_nextToken] = (id: captureId, bytes: bytes);
    return _nextToken;
  }

  /// What the worker forwards to the page for a notification.
  void post(Map<String, Object> message) {
    worker.callMethod<JSAny?>(
      'onmessage'.toJS,
      web.MessageEvent('message', web.MessageEventInit(data: message.jsify())),
    );
  }
}

const _format = MixerOutputFormat.pcmF32le;

void main() {
  test(
    "a stopped capture's packet is freed, the current one emitted",
    () async {
      final module = CaptureModule();
      final bindings = FlutterSoLoudWeb();
      await bindings.setDartEventCallbacks();
      final received = <Uint8List>[];
      final subscription = bindings.mixerOutputChunkEvents.listen(received.add);

      expect(
        bindings.startMixerOutputCapture(
          _format,
          44100,
          2,
          1 << 20,
          4096,
          2048,
        ),
        PlayerErrors.noError,
      );
      final stale = module.queue([1, 2, 3, 4]);
      bindings.stopMixerOutputCapture();
      expect(
        bindings.startMixerOutputCapture(
          _format,
          44100,
          2,
          1 << 20,
          4096,
          2048,
        ),
        PlayerErrors.noError,
      );
      final current = module.queue([5, 6, 7, 8]);
      // The first capture's notification arrives after the restart.
      module
        ..post({'message': 'mixerOutputPacket', 'token': stale, 'length': 4})
        ..post({'message': 'mixerOutputPacket', 'token': current, 'length': 4});
      await Future<void>.delayed(const Duration(milliseconds: 20));

      expect(received, [
        orderedEquals([5, 6, 7, 8]),
      ]);
      expect(module.packets, isEmpty, reason: 'both packets are claimed');
      expect(module.allocations, isEmpty, reason: 'and their copies freed');
      bindings.stopMixerOutputCapture();
      await subscription.cancel();
    },
  );

  test("the shipped wasm build's heap-address messages still arrive", () async {
    final module = CaptureModule(withPackets: false);
    final bindings = FlutterSoLoudWeb();
    await bindings.setDartEventCallbacks();
    final received = <Uint8List>[];
    final subscription = bindings.mixerOutputChunkEvents.listen(received.add);

    expect(
      bindings.startMixerOutputCapture(_format, 44100, 2, 1 << 20, 4096, 2048),
      PlayerErrors.noError,
    );
    final offset = module.place([9, 8, 7, 6]);
    module.post({'message': 'mixerOutputData', 'offset': offset, 'length': 4});
    await Future<void>.delayed(const Duration(milliseconds: 20));

    expect(received, [
      orderedEquals([9, 8, 7, 6]),
    ]);
    bindings.stopMixerOutputCapture();
    await subscription.cancel();
  });

  test('a manager whose binding started nothing stops nothing', () async {
    // The capture another manager, on a binding of its own, started.
    final module = CaptureModule()..running = true;
    final bindings = FlutterSoLoudWeb();
    await bindings.setDartEventCallbacks();
    MixerOutputStreamManager(bindings: bindings, isReady: () => true).stop();
    expect(module.stops, 0);
    expect(module.running, isTrue);

    // Its own capture it does stop.
    final manager = MixerOutputStreamManager(
      bindings: bindings,
      isReady: () => true,
    );
    module.running = false;
    final subscription = manager.start(chunkPCMFrames: 2048).listen((_) {});
    manager.stop();
    expect(module.stops, 1);
    expect(module.running, isFalse);
    await subscription.cancel();
  });
}

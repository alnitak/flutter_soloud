// The web bindings pass Dart strings to native exports that read them as
// NUL-terminated UTF-8, as the FFI bindings do with toNativeUtf8(), and free
// them again even when the export throws. The WASM module is replaced by a
// stand-in whose heap starts dirty, like a block the allocator hands out
// again, which records the C string each export would read and the blocks
// still allocated.
//
// Run with: flutter test --platform chrome test/web_string_args_test.dart
@TestOn('browser')
library;

import 'dart:convert';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/bindings_player_web.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

void main() {
  late ByteData heap;
  late Map<String, String> received;
  late Set<int> allocated;
  late bool exportsThrow;

  String readCString(int ptr) {
    var end = ptr;
    while (end < heap.lengthInBytes && heap.getUint8(end) != 0) {
      end++;
    }
    return utf8.decode(
      heap.buffer.asUint8List(ptr, end - ptr),
      allowMalformed: true,
    );
  }

  setUp(() {
    heap = ByteData(1 << 16);
    for (var i = 0; i < heap.lengthInBytes; i++) {
      heap.setUint8(i, 0xAA);
    }
    received = {};
    allocated = {};
    exportsThrow = false;
    var top = 8;

    /// What each export does last: fail, when the test asks for it.
    void maybeThrow(String export) {
      if (exportsThrow) throw StateError('$export failed');
    }

    final module = JSObject()
      ..['HEAPU8'] = heap.buffer.asUint8List().toJS
      ..['_malloc'] = ((int size) {
        final ptr = top;
        top += (size + 7) & ~7;
        allocated.add(ptr);
        return ptr;
      }).toJS
      ..['_free'] = ((int ptr) {
        allocated.remove(ptr);
      }).toJS
      ..['setValue'] = ((int ptr, int value, String type) {
        if (type != 'i8') throw ArgumentError(type);
        heap.setInt8(ptr, value.toSigned(8));
      }).toJS
      ..['getValue'] = ((int ptr, String type) {
        return heap.getInt32(ptr, Endian.little);
      }).toJS
      ..['_loadMem'] = ((int name, int mem, int length, int mode, int hash) {
        received['loadMem'] = readCString(name);
        heap.setInt32(hash, 1, Endian.little);
        maybeThrow('loadMem');
        return 0;
      }).toJS
      ..['_joinTwoSources'] =
          ((int name, int mem1, int mem2, int len1, int len2, int hash) {
            received['joinTwoSources'] = readCString(name);
            heap.setInt32(hash, 1, Endian.little);
            maybeThrow('joinTwoSources');
            return 0;
          }).toJS
      ..['_speechText'] = ((int text, int handle) {
        received['speechText'] = readCString(text);
        heap.setInt32(handle, 1, Endian.little);
        maybeThrow('speechText');
        return 0;
      }).toJS
      ..['_sendToWorker'] = ((int message, int value) {
        received['sendToWorker'] = readCString(message);
        maybeThrow('sendToWorker');
      }).toJS
      ..['_setCustomFilterParamDefinition'] =
          ((
                int hash,
                int bus,
                int filter,
                int index,
                int name,
                double min,
                double max,
                double fallback,
              ) {
                received['setCustomFilterParamDefinition'] = readCString(name);
                maybeThrow('setCustomFilterParamDefinition');
                return 0;
              })
              .toJS;
    globalContext['Module_soloud'] = module;
  });

  test('loadMem passes its unique name', () {
    final r = FlutterSoLoudWeb().loadMem(
      'sound.mp3',
      Uint8List(16),
      LoadMode.memory,
    );
    expect(r.error, PlayerErrors.noError);
    expect(received['loadMem'], 'sound.mp3');
  });

  test('loadMem passes a non-ASCII unique name as UTF-8', () {
    FlutterSoLoudWeb().loadMem('señal.mp3', Uint8List(16), LoadMode.memory);
    expect(received['loadMem'], 'señal.mp3');
  });

  test('joinTwoSources passes its unique name', () {
    FlutterSoLoudWeb().joinTwoSources('joined', Uint8List(8), Uint8List(8));
    expect(received['joinTwoSources'], 'joined');
  });

  test('speechText passes its text', () {
    final r = FlutterSoLoudWeb().speechText('hello world');
    expect(r.error, PlayerErrors.noError);
    expect(received['speechText'], 'hello world');
  });

  test('sendMessageToWasmWorker passes its message', () {
    FlutterSoLoudWeb().sendMessageToWasmWorker('ping', 1);
    expect(received['sendToWorker'], 'ping');
  });

  test('setCustomFilterParamDefinition passes its name', () {
    final e = FlutterSoLoudWeb().setCustomFilterParamDefinition(
      1,
      0,
      'gain',
      0,
      1,
      0.5,
    );
    expect(e, PlayerErrors.noError);
    expect(received['setCustomFilterParamDefinition'], 'gain');
  });

  group('frees what it allocated when the export throws:', () {
    setUp(() => exportsThrow = true);

    void expectNothingLeftAfter(void Function() call) {
      expect(call, throwsA(anything));
      expect(allocated, isEmpty, reason: 'blocks still allocated');
    }

    test('loadMem', () {
      expectNothingLeftAfter(
        () => FlutterSoLoudWeb().loadMem(
          'sound.mp3',
          Uint8List(16),
          LoadMode.memory,
        ),
      );
    });

    test('joinTwoSources', () {
      expectNothingLeftAfter(
        () => FlutterSoLoudWeb().joinTwoSources(
          'joined',
          Uint8List(8),
          Uint8List(8),
        ),
      );
    });

    test('speechText', () {
      expectNothingLeftAfter(() => FlutterSoLoudWeb().speechText('hello'));
    });

    test('sendMessageToWasmWorker', () {
      expectNothingLeftAfter(
        () => FlutterSoLoudWeb().sendMessageToWasmWorker('ping', 1),
      );
    });

    test('setCustomFilterParamDefinition', () {
      expectNothingLeftAfter(
        () => FlutterSoLoudWeb().setCustomFilterParamDefinition(
          1,
          0,
          'gain',
          0,
          1,
          0.5,
        ),
      );
    });
  });
}

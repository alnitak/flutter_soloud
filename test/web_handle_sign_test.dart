// SoLoud voice handles are unsigned 32-bit: (voice + 1) | (playIndex << 12),
// with a 20-bit play index, so from the 524288th play on bit 31 is set. The
// FFI bindings read them as unsigned, and the web bindings must return the
// same value, not a negative one.
//
// The WASM module is replaced by a stand-in. Its play exports write such a
// handle to their handle out-pointer, read back with getValue('i32') as on
// the real module, and its event worker and buffering bridge deliver the
// handle the way the native side does, as a signed 32-bit number.
//
// Run with: flutter test --platform chrome [--wasm] test/web_handle_sign_test.dart
@TestOn('browser')
library;

import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/bindings_player_web.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/sound_handle.dart';
import 'package:flutter_soloud/src/sound_hash.dart';
import 'package:test/test.dart';
import 'package:web/web.dart' as web;

/// Voice 0 at play index 2^19, the first handle with bit 31 set: 0x80000001.
const int _handle = 1 | (0x80000 << 12);

/// The same 32 bits read as a signed number, as EM_ASM hands them to JS.
final int _signed = _handle.toSigned(32);

/// The hash the stand-in gives every buffer stream it creates.
const int _streamHash = 7;

/// Defines the exports whose arguments are only reached by position: each
/// play export writes [_handle] to its last argument, its handle
/// out-pointer, and each stream export writes [_streamHash] to its first.
const String _positionalExports =
    '''
(function () {
  var m = globalThis.Module_soloud;
  var view = new DataView(m.HEAPU8.buffer);
  ['_play', '_playClocked', '_playScheduled', '_play3dWithLoopPoints',
   '_play3dClocked', '_play3dScheduled', '_busPlayOnEngine',
   '_speechText'].forEach(
    function (name) {
      m[name] = function () {
        view.setUint32(arguments[arguments.length - 1], $_handle, true);
        return 0;
      };
    });
  ['_setBufferStream', '_setPullBufferStream'].forEach(function (name) {
    m[name] = function () {
      view.setUint32(arguments[0], $_streamHash, true);
      return 0;
    };
  });
})();
''';

void main() {
  late ByteData heap;
  late JSObject worker;

  setUp(() {
    // The heap is a JS Uint8Array, as on the real module, and Dart reads it
    // through a view of that array. Converting a Dart Uint8List with .toJS
    // instead would copy it on dart2wasm, so the stand-in exports would
    // write to a copy that getValue never reads.
    final heapU8 = JSUint8Array.withLength(1 << 16);
    heap = ByteData.sublistView(heapU8.toDart);
    var top = 8;
    worker = JSObject();
    globalContext['Module_soloud'] = JSObject()
      ..['HEAPU8'] = heapU8
      ..['wasmWorker'] = worker
      ..['_malloc'] = ((int size) {
        final ptr = top;
        top += (size + 7) & ~7;
        return ptr;
      }).toJS
      ..['_free'] = ((int ptr) {}).toJS
      // Emscripten's getValue: 'i32' is signed.
      ..['getValue'] = ((int ptr, String type) {
        if (type != 'i32' && type != '*') throw ArgumentError(type);
        return heap.getInt32(ptr, Endian.little);
      }).toJS
      ..['_createWorkerInWasm'] = (() => 1).toJS
      ..['_setMixerOutputCallback'] = ((int callback) {}).toJS;
    globalContext.callMethod<JSAny?>('eval'.toJS, _positionalExports.toJS);
  });

  final hash = SoundHash.fromValue(1);
  final plays = <String, SoundHandle Function(FlutterSoLoudWeb)>{
    'play': (b) => b.play(hash).newHandle,
    'playClocked': (b) => b.playClocked(hash, Duration.zero).newHandle,
    'playScheduled': (b) => b.playScheduled(hash, Duration.zero).newHandle,
    'play3d': (b) => b.play3d(hash, 0, 0, 0).newHandle,
    'play3dClocked': (b) =>
        b.play3dClocked(hash, Duration.zero, 0, 0, 0).newHandle,
    'play3dScheduled': (b) =>
        b.play3dScheduled(hash, Duration.zero, 0, 0, 0).newHandle,
    'busPlayOnEngine': (b) => b.busPlayOnEngine(1, 1, false).handle,
    'speechText': (b) => b.speechText('hi').handle,
  };
  for (final MapEntry(key: name, value: play) in plays.entries) {
    test('$name returns a handle with bit 31 set unsigned', () {
      expect(play(FlutterSoLoudWeb()).id, _handle);
    });
  }

  test('a voice-ended event carries the same unsigned handle', () async {
    final bindings = FlutterSoLoudWeb();
    await bindings.setDartEventCallbacks();
    final ended = <int>[];
    final sub = bindings.voiceEndedEventController.stream.listen(ended.add);
    // What bindings.cpp posts for the ended voice: the handle as an int.
    worker.callMethod<JSAny?>(
      'onmessage'.toJS,
      web.MessageEvent(
        'message',
        web.MessageEventInit(
          data: {'message': 'voiceEndedCallback', 'value': _signed}.jsify(),
        ),
      ),
    );
    await Future<void>.delayed(const Duration(milliseconds: 20));
    expect(ended, [_handle]);
    await sub.cancel();
  });

  // What audiobuffer.cpp and pull_buffer_stream.cpp do through EM_ASM:
  // window["dartOnBufferingCallback_" + hash](isBuffering, handle, time).
  void deliverBuffering() {
    globalContext.callMethod<JSAny?>(
      'dartOnBufferingCallback_$_streamHash'.toJS,
      true.toJS,
      _signed.toJS,
      0.5.toJS,
    );
  }

  test('a buffer stream buffering event carries the unsigned handle', () {
    final handles = <int>[];
    final r = FlutterSoLoudWeb().setBufferStream(
      1 << 20,
      BufferingType.preserved,
      1,
      44100,
      2,
      0,
      (isBuffering, handle, time) => handles.add(handle),
      null,
    );
    expect(r.error, PlayerErrors.noError);
    deliverBuffering();
    expect(handles, [_handle]);
  });

  test('a pull stream buffering event carries the unsigned handle', () {
    final handles = <int>[];
    final r = FlutterSoLoudWeb().setPullBufferStream(
      1 << 20,
      0.5,
      44100,
      2,
      0,
      1 << 20,
      (isBuffering, handle, time) => handles.add(handle),
      null,
      null,
      null,
    );
    expect(r.error, PlayerErrors.noError);
    deliverBuffering();
    expect(handles, [_handle]);
  });
}

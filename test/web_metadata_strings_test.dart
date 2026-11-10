// The web reflection of metadata_ffi.h reads C strings out of the WASM heap.
// The native side stores UTF-8, so a tag outside ASCII must decode as UTF-8.
// The WASM module is replaced by a stand-in heap with Emscripten's getValue().
@TestOn('browser')
library;

import 'dart:convert';
import 'dart:js_interop';
import 'dart:js_interop_unsafe';
import 'dart:typed_data';

import 'package:flutter_soloud/src/bindings/native_metadata_web.dart';
import 'package:test/test.dart';

void main() {
  final heap = ByteData(1 << 16);

  setUp(() {
    for (var i = 0; i < heap.lengthInBytes; i++) {
      heap.setUint8(i, 0);
    }
    globalContext['Module_soloud'] = JSObject()
      ..['getValue'] = ((int ptr, String type) {
        if (type == 'i8') return heap.getInt8(ptr);
        throw ArgumentError.value(type, 'type');
      }).toJS;
  });

  void putBytes(int at, List<int> bytes) {
    for (var i = 0; i < bytes.length; i++) {
      heap.setUint8(at + i, bytes[i]);
    }
  }

  // An Mp3MetadataFFI: title at 0, artist at 1024, album at 3072.
  const base = 64;

  test('metadata strings decode as UTF-8', () {
    putBytes(base, utf8.encode('Café del Mar'));
    putBytes(base + 1024, utf8.encode('Sigur Rós 🎵'));
    putBytes(base + 3072, utf8.encode('Plain ASCII'));
    final mp3 = NativeMp3Metadata(base);
    expect(mp3.title, 'Café del Mar');
    expect(mp3.artist, 'Sigur Rós 🎵');
    expect(mp3.album, 'Plain ASCII');
  });

  test('a malformed sequence decodes to U+FFFD', () {
    // 0xc3 starts a two-byte sequence that '(' does not continue.
    putBytes(base, [0xc3, 0x28]);
    expect(NativeMp3Metadata(base).title, '�(');
  });
}

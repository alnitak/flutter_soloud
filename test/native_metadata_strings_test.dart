// metadata_ffi.h stores tags as UTF-8 in fixed char arrays. The FFI
// reflection must decode them as UTF-8 whether `char` is signed (Apple, x86)
// or unsigned (Android ARM).
@TestOn('vm')
library;

import 'dart:convert';
import 'dart:ffi' as ffi;

import 'package:ffi/ffi.dart';
import 'package:flutter_soloud/src/bindings/native_metadata_ffi.dart';
import 'package:test/test.dart';

/// Writes [bytes] into [array], NUL-terminated when there is room.
void _putBytes(ffi.Array<ffi.Char> array, List<int> bytes, int capacity) {
  for (var i = 0; i < bytes.length; i++) {
    array[i] = bytes[i];
  }
  if (bytes.length < capacity) array[bytes.length] = 0;
}

void _put(ffi.Array<ffi.Char> array, String s) =>
    _putBytes(array, utf8.encode(s), 1024);

void main() {
  test('metadata strings decode as UTF-8', () {
    final meta = calloc<NativeAudioMetadata>();
    addTearDown(() => calloc.free(meta));

    _put(meta.ref.mp3Metadata.title, 'Café del Mar');
    _put(meta.ref.mp3Metadata.artist, 'Sigur Rós');
    _put(meta.ref.mp3Metadata.album, 'Plain ASCII');
    _put(meta.ref.m4aMetadata.title, 'Ünïcödé 🎵');
    _put(meta.ref.oggMetadata.vendor, 'Lavf 61 – libopus');
    meta.ref.oggMetadata.commentsCount = 1;
    _put(meta.ref.oggMetadata.comments[0].key, 'TITLE');
    _put(meta.ref.oggMetadata.comments[0].value, 'Jóga');

    final result = meta.ref.toAudioMetadata();
    expect(result.mp3Metadata!.title, 'Café del Mar');
    expect(result.mp3Metadata!.artist, 'Sigur Rós');
    expect(result.mp3Metadata!.album, 'Plain ASCII');
    expect(result.m4aMetadata!.title, 'Ünïcödé 🎵');
    expect(result.oggMetadata!.vendor, 'Lavf 61 – libopus');
    expect(result.oggMetadata!.comments, {'TITLE': 'Jóga'});
  });

  test('metadata strings stay bounded and tolerate malformed UTF-8', () {
    final meta = calloc<NativeAudioMetadata>();
    addTearDown(() => calloc.free(meta));

    _put(meta.ref.mp3Metadata.title, '  Café  ');
    // 0xc3 starts a two-byte sequence that '(' does not continue.
    _putBytes(meta.ref.aacMetadata.title, [0xc3, 0x28], 1024);
    // Both 32-byte fields filled to capacity, with no NUL: 16 'é'.
    final full = List.generate(32, (i) => i.isEven ? 0xc3 : 0xa9);
    _putBytes(meta.ref.aacMetadata.profile, full, 32);
    _putBytes(meta.ref.m4aMetadata.codec, full, 32);

    final result = meta.ref.toAudioMetadata();
    expect(result.mp3Metadata!.title, 'Café');
    expect(result.aacMetadata!.title, '�(');
    expect(result.aacMetadata!.profile, 'é' * 16);
    expect(result.m4aMetadata!.codec, 'é' * 16);
  });
}

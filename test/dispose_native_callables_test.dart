import 'dart:ffi' as ffi;

import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';
import 'package:test/test.dart';

void main() {
  test('disposeNativeCallables does not clear native registrations', () {
    final bindings = FlutterSoLoudFfi.fromLookup(<T extends ffi.NativeType>(
      String symbol,
    ) {
      throw StateError('Native lookup should not be called for $symbol.');
    });

    expect(bindings.disposeNativeCallables, returnsNormally);
  });
}

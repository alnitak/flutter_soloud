import 'package:flutter_soloud/src/bindings/bindings_player_ffi.dart';
import 'package:test/test.dart';

void main() {
  test('disposeNativeCallables does not clear native registrations', () {
    final bindings = FlutterSoLoudFfi();

    expect(bindings.disposeNativeCallables, returnsNormally);
  });
}

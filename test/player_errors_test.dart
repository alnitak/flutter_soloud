import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

void main() {
  test('PlayerErrors value corresponds to its position', () {
    for (final error in PlayerErrors.values) {
      expect(
        error.value,
        PlayerErrors.values.indexOf(error),
        reason: 'The value of $error is ${error.value} '
            'but its position in the PlayerErrors enum is '
            '${PlayerErrors.values.indexOf(error)}. '
            'This makes code such as `final error = PlayerErrors.values[ret];` '
            'invalid.',
      );
    }
  });
}

import 'package:flutter_soloud/src/enums.dart';
import 'package:test/test.dart';

void main() {
  test('MixerOutputFormat value corresponds to its position', () {
    for (final format in MixerOutputFormat.values) {
      expect(
        format.value,
        MixerOutputFormat.values.indexOf(format),
        reason:
            'The value of $format is ${format.value} '
            'but its position in the MixerOutputFormat enum is '
            '${MixerOutputFormat.values.indexOf(format)}. '
            'This makes code such as `final format = '
            'MixerOutputFormat.values[ret];` invalid.',
      );
    }
  });
}

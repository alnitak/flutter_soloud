import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  SoLoud.instance.setFxParams(FilterType.freeverbFilter, 0, 0.5);
  SoLoud.instance.getFxParams(FilterType.freeverbFilter, 0);
}

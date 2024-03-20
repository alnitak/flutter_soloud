import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.stopIsolate();
  await SoLoud.instance.dispose();
  await SoLoud.instance.shutdown();
}

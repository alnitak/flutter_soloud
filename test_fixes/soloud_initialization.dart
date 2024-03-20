import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  final soloud = SoLoud();
  await soloud.startIsolate();
  await soloud.initialize();
}

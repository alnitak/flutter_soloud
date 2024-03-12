import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  final notes = await SoloudTools.initSounds();
  print(notes);
}

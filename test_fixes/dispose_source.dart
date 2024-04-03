import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  final sound = AudioSource(SoundHash.invalid());
  SoLoud.instance.disposeSound(sound);
  SoLoud.instance.disposeAllSound();
}

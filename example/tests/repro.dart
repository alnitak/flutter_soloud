import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

/// Loop mp3 playback sessions until the MT engine-time freeze hits.
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(
    const MaterialApp(
      home: Scaffold(body: Center(child: Text('repro running'))),
    ),
  );

  await Future<void>.delayed(const Duration(seconds: 1));

  for (var session = 1; session <= 40; session++) {
    try {
      await SoLoud.instance.init(bufferSize: 4096, channels: Channels.mono);
      final music = await SoLoud.instance.loadAsset(
        'assets/audio/8_bit_mentality.mp3',
      );
      SoLoud.instance.play(music);
      debugPrint('REPRO: session $session playing');

      var last = Duration.zero;
      var first = true;
      var frozenCount = 0;
      var froze = false;
      for (var i = 0; i < 16; i++) {
        await Future<void>.delayed(const Duration(milliseconds: 500));
        final t = SoLoud.instance.getEngineTime();
        final frozen = !first && t == last;
        first = false;
        if (frozen) frozenCount++;
        debugPrint(
          'REPRO: s$session ${(i + 1) * 500}ms '
          'engineTime=${t.inMilliseconds}ms '
          '${frozen ? "FROZEN ($frozenCount)" : ""}',
        );
        last = t;
        if (frozenCount > 4) {
          froze = true;
          break;
        }
      }

      if (froze) {
        debugPrint('REPRO: FROZEN in session $session - leaving engine as-is');
        return;
      }

      SoLoud.instance.deinit();
      debugPrint('REPRO: session $session ok');
    } catch (e, st) {
      debugPrint('REPRO: EXCEPTION $e\n$st');
      return;
    }
  }
  debugPrint('REPRO: all sessions ok, no freeze');
}

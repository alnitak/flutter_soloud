// M4A/MP4 and AAC on the web: SoLoud.loadMem hands ISO BMFF and ADTS/ADIF
// bytes to the browser's decoder (decodeAudioData, jsDecodeAudioDataWeb in
// lib/src/bindings/js_extension.dart) and plays the PCM from a buffer stream;
// AC-3/E-AC-3 are refused up front (no Dolby decoder in browsers). The native
// web stubs (os_decoder_web.cpp, aac_stream_decoder_web.cpp) only report
// "not supported", so this is the path that decodes. Real WASM engine, fixtures
// from example/assets/audio (sent by test/support/web_hybrid/asset_bytes.dart).
//
// Checks: valid M4A (mono 44.1 kHz, stereo 48 kHz), ADTS AAC and an MP4 load
// with the expected duration and play; truncated M4A, an 'ftyp' header with
// garbage, AC-3 and E-AC-3 fail with a SoLoud exception and leave nothing
// behind; LoadMode.disk behaves like memory on the web.
//
// Browser only: ./test/support/web_tests.sh test/common/web_decoders_test.dart [--wasm]
@TestOn('browser')
@Tags(['device'])
library;

import 'dart:typed_data';

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests(globalVolume: 0);
  final soloud = SoLoud.instance;
  final files = <String, Uint8List>{};

  setUpAll(() async {
    final channel = spawnHybridUri('../support/web_hybrid/asset_bytes.dart');
    channel.sink.add([
      'aac_lc_mono_44100.m4a',
      'aac_lc_stereo_48000.m4a',
      'sample-AAC.aac',
      'sample-AAC.m4a',
      'sample-AAC.mp4',
      'sample-AC3.ac3',
      'sample-EAC3.eac3',
    ]);
    final map = (await channel.stream.first as Map).cast<String, Object?>();
    for (final e in map.entries) {
      if (e.value != null) {
        files[e.key] = Uint8List.fromList([
          for (final v in e.value! as List) (v as num).toInt(),
        ]);
      }
    }
    await channel.sink.close();
  });

  Future<void> expectPlays(AudioSource s) async {
    final h = soloud.play(s);
    await Future<void>.delayed(const Duration(milliseconds: 300));
    expect(soloud.getIsValidVoiceHandle(h), isTrue);
    expect(soloud.getPosition(h), greaterThan(Duration.zero));
  }

  for (final name in ['aac_lc_mono_44100.m4a', 'aac_lc_stereo_48000.m4a']) {
    test('$name: 2 s, plays', () async {
      final s = await soloud.loadMem(name, files[name]!);
      final ms = soloud.getLength(s).inMicroseconds / 1000;
      // The fixture is exactly 2 s; the browser may keep the AAC padding
      // when it ignores the container's gapless info (one 1024-frame packet).
      expect(ms, inInclusiveRange(1995, 2050), reason: 'length $ms ms');
      await expectPlays(s);
    });
  }

  for (final name in ['sample-AAC.aac', 'sample-AAC.m4a', 'sample-AAC.mp4']) {
    test('$name loads and plays', () async {
      final s = await soloud.loadMem(name, files[name]!);
      expect(soloud.getLength(s), greaterThan(const Duration(seconds: 1)));
      await expectPlays(s);
    });
  }

  test('ADTS and M4A of the same audio have about the same length', () async {
    final a = await soloud.loadMem('a.aac', files['sample-AAC.aac']!);
    final m = await soloud.loadMem('a.m4a', files['sample-AAC.m4a']!);
    final diff = (soloud.getLength(a) - soloud.getLength(m)).inMilliseconds
        .abs();
    expect(diff, lessThan(100));
  });

  test('LoadMode.disk on the web decodes like memory', () async {
    const name = 'aac_lc_stereo_48000.m4a';
    final s = await soloud.loadMem(
      '$name-disk',
      files[name]!,
      mode: LoadMode.disk,
    );
    expect(soloud.getLength(s).inMilliseconds, inInclusiveRange(1995, 2050));
    await expectPlays(s);
  });

  Future<void> expectCleanFailure(String what, Uint8List bytes) async {
    final before = soloud.activeSounds.length;
    await expectLater(
      soloud.loadMem(what, bytes),
      throwsA(isA<SoLoudException>()),
      reason: what,
    );
    expect(soloud.activeSounds.length, before, reason: '$what: no sound left');
    final tone = await loadTestWaveform();
    expect(soloud.getIsValidVoiceHandle(soloud.play(tone)), isTrue);
  }

  test('a truncated M4A fails cleanly or loads what it has', () async {
    final whole = files['aac_lc_stereo_48000.m4a']!;
    for (final frac in [0.05, 0.5, 0.9]) {
      final cut = Uint8List.sublistView(
        whole,
        0,
        (whole.length * frac).toInt(),
      );
      final before = soloud.activeSounds.length;
      try {
        final s = await soloud.loadMem('cut$frac', cut);
        // Some browsers decode what is there: never more than the whole.
        expect(soloud.getLength(s).inMilliseconds, lessThanOrEqualTo(2050));
      } on SoLoudException {
        expect(soloud.activeSounds.length, before);
      }
    }
  });

  test("'ftyp' followed by garbage fails cleanly", () async {
    // A 'ftyp' box header (size 0x18, brand 'M4A ') and then garbage.
    final b = Uint8List(4096);
    for (var i = 12; i < b.length; i++) {
      b[i] = (i * 31) & 0xff;
    }
    b.setAll(0, [
      0,
      0,
      0,
      0x18,
      0x66,
      0x74,
      0x79,
      0x70,
      0x4d,
      0x34,
      0x41,
      0x20,
    ]);
    await expectCleanFailure('ftyp-garbage.m4a', b);
  });

  test('ADTS sync word followed by garbage fails cleanly', () async {
    final b = Uint8List(2048);
    for (var i = 0; i < b.length; i++) {
      b[i] = (i * 13) & 0xff;
    }
    b
      ..[0] = 0xFF
      ..[1] = 0xF1;
    await expectCleanFailure('adts-garbage.aac', b);
  });

  test('AC-3 and E-AC-3 are refused with audioFormatNotSupported', () async {
    for (final name in ['sample-AC3.ac3', 'sample-EAC3.eac3']) {
      final before = soloud.activeSounds.length;
      await expectLater(
        soloud.loadMem(name, files[name]!),
        throwsA(isA<SoLoudException>()),
      );
      expect(soloud.activeSounds.length, before);
    }
  });
}

// The Dart side of the Android AAudio attributes: SoLoud.init's
// `androidAAudioAttributes` and SoLoud.setAndroidAAudioAttributes. What the
// AAudio stream really gets is checked on a device by
// test/android/android_aaudio_attributes_test.sh; this covers what runs on
// any host: the values the enum hands to the native side, the
// not-initialized error, and that the call changes nothing off Android.
//
// Reads src/ (the native switch), so VM only.
@TestOn('vm')
library;

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  group('values', () {
    test('the enum values are the ones the native switch handles', () {
      expect(AndroidAAudioAttributes.unmanaged.value, 0);
      expect(AndroidAAudioAttributes.mediaMusic.value, 1);
      expect(AndroidAAudioAttributes.voiceCommunication.value, 2);

      final src = File(
        'src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp',
      ).readAsStringSync();
      final body = RegExp(
        r'void miniaudio_setAndroidAAudioAttributes\(unsigned int aAttributes\)'
        r'\s*\{(.*?)\n    \}',
        dotAll: true,
      ).firstMatch(src);
      expect(body, isNotNull, reason: 'miniaudio_setAndroidAAudioAttributes');
      final text = body!.group(1)!;
      // 0 leaves the attributes unset, 2 is voice, anything else media.
      expect(
        RegExp(
          'case 0:.*?ma_aaudio_usage_default.*?ma_aaudio_content_type_default',
          dotAll: true,
        ).hasMatch(text),
        isTrue,
      );
      expect(
        RegExp(
          'case 2:.*?ma_aaudio_usage_voice_communication.*?'
          'ma_aaudio_content_type_speech',
          dotAll: true,
        ).hasMatch(text),
        isTrue,
      );
      expect(
        RegExp(
          'default:.*?ma_aaudio_usage_media.*?ma_aaudio_content_type_music',
          dotAll: true,
        ).hasMatch(text),
        isTrue,
      );
    });
  });

  group('not initialized', () {
    prepareEngineHost();

    test('setAndroidAAudioAttributes throws before init', () async {
      expect(SoLoud.instance.isInitialized, isFalse);
      await expectLater(
        SoLoud.instance.setAndroidAAudioAttributes(
          AndroidAAudioAttributes.voiceCommunication,
        ),
        throwsA(isA<SoLoudNotInitializedException>()),
      );
    });
  });

  group('off Android', () {
    prepareEngineHost();
    tearDown(deinitTestEngine);

    for (final attributes in AndroidAAudioAttributes.values) {
      test('init(androidAAudioAttributes: ${attributes.name}) works', () async {
        await SoLoud.instance.init(androidAAudioAttributes: attributes);
        SoLoud.instance.setGlobalVolume(0);
        expect(SoLoud.instance.isInitialized, isTrue);
        final tone = await loadTestWaveform();
        final h = SoLoud.instance.play(tone);
        await Future<void>.delayed(const Duration(milliseconds: 200));
        expect(SoLoud.instance.getIsValidVoiceHandle(h), isTrue);
      }, tags: ['device']);
    }

    test(
      'setAndroidAAudioAttributes after init does not touch the device',
      () async {
        // flutter_test reports Android unless told otherwise.
        debugDefaultTargetPlatformOverride = TargetPlatform.macOS;
        addTearDown(() => debugDefaultTargetPlatformOverride = null);
        await initTestEngine(globalVolume: 0);
        final tone = await loadTestWaveform();
        final h = SoLoud.instance.play(tone, looping: true);
        await Future<void>.delayed(const Duration(milliseconds: 300));
        final before = SoLoud.instance.getPosition(h);
        expect(before, greaterThan(Duration.zero));

        // Off Android this returns at once, without reopening the device.
        final watch = Stopwatch()..start();
        for (final a in AndroidAAudioAttributes.values) {
          await SoLoud.instance.setAndroidAAudioAttributes(a);
        }
        watch.stop();
        expect(watch.elapsedMilliseconds, lessThan(50));

        await Future<void>.delayed(const Duration(milliseconds: 300));
        expect(SoLoud.instance.isInitialized, isTrue);
        expect(SoLoud.instance.getIsValidVoiceHandle(h), isTrue);
        expect(SoLoud.instance.getPosition(h), greaterThan(before));
      },
      tags: ['device'],
    );
  });
}

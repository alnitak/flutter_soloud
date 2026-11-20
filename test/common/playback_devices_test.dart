// Output devices with the real engine: listPlaybackDevices (before and after
// init: not empty, one default, unique ids) and changeDevice (to the default,
// to the listed default device, to ids that do not exist, while a sound
// plays). Tests that need a listed device are skipped on hosts with none.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

// ignore_for_file: invalid_use_of_internal_member

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  /// The listed devices, or null (and the test skipped) if there are none.
  List<PlaybackDevice>? devicesOrSkip() {
    final devices = soloud.listPlaybackDevices();
    if (devices.isEmpty) {
      markTestSkipped('no playback device on this host');
      return null;
    }
    return devices;
  }

  group('listPlaybackDevices', () {
    test('works before init', () {
      expect(soloud.isInitialized, isFalse);
      final devices = devicesOrSkip();
      if (devices == null) return;
      expect(devices, isNotEmpty);
    });

    test('lists exactly one default, with unique ids and names', () async {
      await initTestEngine();
      final devices = devicesOrSkip();
      if (devices == null) return;
      expect(devices.where((d) => d.isDefault), hasLength(1));
      expect(devices.map((d) => d.id).toSet(), hasLength(devices.length));
      for (final d in devices) {
        expect(d.name, isNotEmpty, reason: '$d');
        expect(d.id, greaterThanOrEqualTo(0));
      }
    });

    test('gives the same list before and after init', () async {
      final before = soloud.listPlaybackDevices();
      await initTestEngine();
      final after = soloud.listPlaybackDevices();
      expect(
        after.map((d) => (d.id, d.name, d.isDefault)),
        before.map((d) => (d.id, d.name, d.isDefault)),
      );
    });
  });

  group('changeDevice', () {
    setUp(initTestEngine);

    test('to the system default works', () async {
      await soloud.changeDevice();
      expect(soloud.isInitialized, isTrue);
    });

    test('to the listed default device works', () async {
      final devices = devicesOrSkip();
      if (devices == null) return;
      await soloud.changeDevice(
        newDevice: devices.firstWhere((d) => d.isDefault),
      );
      expect(soloud.isInitialized, isTrue);
    });

    test('to every listed device works', () async {
      final devices = devicesOrSkip();
      if (devices == null) return;
      for (final d in devices) {
        await soloud.changeDevice(newDevice: d);
      }
      await soloud.changeDevice();
    });

    test('to an id that is not listed throws', () async {
      final count = soloud.listPlaybackDevices().length;
      await expectLater(
        soloud.changeDevice(
          newDevice: PlaybackDevice(count + 100, false, 'nope'),
        ),
        throwsA(isA<SoLoudNoPlaybackDevicesFoundCppException>()),
      );
      expect(soloud.isInitialized, isTrue);
    });

    test('to a negative id throws', () async {
      await expectLater(
        soloud.changeDevice(newDevice: const PlaybackDevice(-5, false, 'x')),
        throwsA(isA<SoLoudInvalidParameterException>()),
      );
    });

    test('while a sound plays keeps the voice playing and audible', () async {
      soloud.setGlobalVolume(0.05);
      final tone = await loadTestWaveform();
      final h = soloud.play(tone);
      await Future<void>.delayed(const Duration(milliseconds: 200));
      final before = soloud.getPosition(h);
      await soloud.changeDevice();
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.getPause(h), isFalse);
      expect(
        await waitUntil(() => soloud.getPosition(h) > before),
        isTrue,
        reason: 'the voice advances on the new device',
      );
      final out = await captureOutput();
      expect(out.rms(0), greaterThan(0.005));
      expect(out.frequency(0), closeTo(440, 5));
    });
  });
}

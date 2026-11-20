// Voice groups with the real engine: creation and destruction, operations on
// the group handle reaching every member, empty groups, and adding handles
// that are not (or no longer) voices.
@Tags(['device'])
library;

import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

void main() {
  setUpEngineTests(globalVolume: 0);
  final soloud = SoLoud.instance;

  late AudioSource tone;
  late List<SoundHandle> voices;
  late SoundHandle vg;

  setUp(() async {
    tone = await loadTestWaveform();
    voices = [for (var i = 0; i < 3; i++) soloud.play(tone, paused: true)];
    vg = soloud.createVoiceGroup();
  });

  group('lifecycle', () {
    test('a new group is a valid, empty group handle', () {
      expect(vg.isError, isFalse);
      expect(soloud.isVoiceGroup(vg), isTrue);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('voice handles are not groups', () {
      for (final v in voices) {
        expect(soloud.isVoiceGroup(v), isFalse);
      }
    });

    test('each group gets its own handle', () {
      final other = soloud.createVoiceGroup();
      expect(other, isNot(vg));
      expect(soloud.isVoiceGroup(other), isTrue);
    });

    test('destroying a group invalidates it but keeps its voices', () {
      soloud
        ..addVoicesToGroup(vg, voices)
        ..destroyVoiceGroup(vg);
      expect(soloud.isVoiceGroup(vg), isFalse);
      for (final v in voices) {
        expect(soloud.getIsValidVoiceHandle(v), isTrue);
      }
    });

    test('destroying a group twice is harmless', () {
      soloud
        ..destroyVoiceGroup(vg)
        ..destroyVoiceGroup(vg);
      expect(soloud.isVoiceGroup(vg), isFalse);
    });

    test('deinit drops the groups', () async {
      soloud.deinit();
      await initTestEngine(globalVolume: 0);
      expect(soloud.isVoiceGroup(vg), isFalse);
    });
  });

  group('membership', () {
    test('adding voices makes the group non-empty', () {
      soloud.addVoicesToGroup(vg, voices);
      expect(soloud.isVoiceGroupEmpty(vg), isFalse);
    });

    test('adding an empty list keeps the group empty', () {
      soloud.addVoicesToGroup(vg, []);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('handles that are not voices are not added', () {
      soloud.addVoicesToGroup(vg, [
        const SoundHandle(0),
        const SoundHandle(0x0BAD0BAD),
      ]);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('a group cannot be added to another group', () {
      final inner = soloud.createVoiceGroup();
      soloud
        ..addVoicesToGroup(inner, voices)
        ..addVoicesToGroup(vg, [inner]);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('adding to a destroyed group does nothing', () {
      soloud
        ..destroyVoiceGroup(vg)
        ..addVoicesToGroup(vg, voices);
      expect(soloud.isVoiceGroup(vg), isFalse);
    });

    test('a voice that ended is not added', () async {
      final short = await loadTestSound(
        duration: const Duration(milliseconds: 30),
      );
      final h = soloud.play(short);
      expect(await waitUntil(() => !soloud.getIsValidVoiceHandle(h)), isTrue);
      soloud.addVoicesToGroup(vg, [h]);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('the group is trimmed of voices that end', () async {
      final short = await loadTestSound(
        duration: const Duration(milliseconds: 50),
      );
      final a = soloud.play(short);
      final b = soloud.play(short);
      soloud.addVoicesToGroup(vg, [a, b]);
      expect(soloud.isVoiceGroupEmpty(vg), isFalse);
      expect(await waitUntil(() => soloud.isVoiceGroupEmpty(vg)), isTrue);
      expect(soloud.isVoiceGroup(vg), isTrue);
    });

    test('a voice stopped on its own leaves the others in the group', () async {
      soloud.addVoicesToGroup(vg, [voices.first]);
      await soloud.stop(voices.first);
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });
  });

  group('operations on the group handle', () {
    setUp(() => soloud.addVoicesToGroup(vg, voices));

    test('setPause reaches every member', () {
      soloud.setPause(vg, false);
      for (final v in voices) {
        expect(soloud.getPause(v), isFalse);
      }
      soloud.setPause(vg, true);
      for (final v in voices) {
        expect(soloud.getPause(v), isTrue);
      }
    });

    test('setVolume, setPan and setRelativePlaySpeed reach every member', () {
      soloud
        ..setVolume(vg, 0.3)
        ..setPan(vg, -0.4)
        ..setRelativePlaySpeed(vg, 1.5);
      for (final v in voices) {
        expect(soloud.getVolume(v), closeTo(0.3, 1e-6));
        expect(soloud.getPan(v), closeTo(-0.4, 1e-6));
        expect(soloud.getRelativePlaySpeed(v), closeTo(1.5, 1e-6));
      }
    });

    test('members outside the group are untouched', () {
      final outsider = soloud.play(tone, paused: true, volume: 0.7);
      soloud
        ..setVolume(vg, 0.1)
        ..setPause(vg, false);
      expect(soloud.getVolume(outsider), closeTo(0.7, 1e-6));
      expect(soloud.getPause(outsider), isTrue);
    });

    test('fadeVolume reaches every member', () async {
      soloud
        ..setPause(vg, false)
        ..fadeVolume(vg, 0.2, const Duration(milliseconds: 100));
      expect(
        await waitUntil(
          () => voices.every((v) => (soloud.getVolume(v) - 0.2).abs() < 1e-3),
        ),
        isTrue,
      );
    });

    test('scheduleStop stops every member', () async {
      soloud
        ..setPause(vg, false)
        ..scheduleStop(vg, const Duration(milliseconds: 50));
      expect(
        await waitUntil(
          () => voices.every((v) => !soloud.getIsValidVoiceHandle(v)),
        ),
        isTrue,
      );
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
    });

    test('stop stops every member', () async {
      await soloud.stop(vg);
      for (final v in voices) {
        expect(soloud.getIsValidVoiceHandle(v), isFalse);
      }
      expect(soloud.isVoiceGroupEmpty(vg), isTrue);
      expect(soloud.isVoiceGroup(vg), isTrue);
    });

    test('operations on an empty group do nothing', () {
      final empty = soloud.createVoiceGroup();
      expect(() => soloud.setPause(empty, false), returnsNormally);
      soloud.setVolume(empty, 0.1);
      for (final v in voices) {
        expect(soloud.getPause(v), isTrue);
        expect(soloud.getVolume(v), 1);
      }
    });
  });
}

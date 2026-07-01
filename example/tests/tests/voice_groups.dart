import 'package:flutter_soloud/flutter_soloud.dart';

import 'common.dart';

/// Test voice groups.
Future<OutputBuffer> testVoiceGroups() async {
  await initialize();

  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  /// Start playing sounds in pause state to get their handles.
  final h1 = SoLoud.instance.play(currentSound, paused: true);
  final h2 = SoLoud.instance.play(currentSound, paused: true);
  final h3 = SoLoud.instance.play(currentSound, paused: true);
  final h4 = SoLoud.instance.play(currentSound, paused: true);

  final group = SoLoud.instance.createVoiceGroup();
  assert(!group.isError, 'Failed to create voice group!');

  var isValid = SoLoud.instance.isVoiceGroup(group);
  assert(isValid, 'Voice group created but it is not valid!');

  var isEmpty = SoLoud.instance.isVoiceGroupEmpty(group);
  assert(isEmpty, 'Voice group just created but it is not empty!');

  /// Add all voices to the group.
  SoLoud.instance.addVoicesToGroup(group, [h1, h2, h3, h4]);
  isEmpty = SoLoud.instance.isVoiceGroupEmpty(group);
  assert(!isEmpty, 'Voices added to the group, but the group is empty!');

  /// Start playing the voices in the group.
  SoLoud.instance.setPause(group, false);

  await delay(4000);

  /// Check if group is empty after playing voices.
  isEmpty = SoLoud.instance.isVoiceGroupEmpty(group);
  assert(
    isEmpty,
    'Voices added and finished to play, but the group is not empty!',
  );

  /// Destroy the group
  SoLoud.instance.destroyVoiceGroup(group);
  isValid = SoLoud.instance.isVoiceGroup(group);
  assert(!isValid, 'Voice group destroy failed!');

  deinit();
  return OutputBuffer();
}

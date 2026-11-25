// Every public member of SoLoud, called before init() and after deinit(),
// throws SoLoudNotInitializedException, or is listed in _exempt with the
// reason it does not. The list of members is checked against the public
// members parsed from lib/src/soloud.dart, so a new method cannot be left out.
@Tags(['device'])
library;

import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/sounds.dart';

// ignore_for_file: deprecated_member_use_from_same_package
// ignore_for_file: experimental_member_use, deprecated_member_use

const _soloudPath = 'lib/src/soloud.dart';

typedef _Call = Object? Function(SoLoud s, AudioSource sound, SoundHandle h);

/// Every member that must throw SoLoudNotInitializedException without an
/// initialized engine, with a call that reaches its init check.
final Map<String, _Call> _calls = {
  'changeDevice': (s, _, _) => s.changeDevice(),
  'setAndroidAAudioAttributes': (s, _, _) =>
      s.setAndroidAAudioAttributes(AndroidAAudioAttributes.mediaMusic),
  'stopAudioDevice': (s, _, _) => s.stopAudioDevice(),
  'startAudioDevice': (s, _, _) => s.startAudioDevice(),
  'loadFile': (s, _, _) => s.loadFile('missing.wav'),
  'loadMem': (s, _, _) => s.loadMem('x.wav', wavBytes()),
  'joinTwoSources': (s, _, _) => s.joinTwoSources('j', wavBytes(), wavBytes()),
  'startMixerOutputStream': (s, _, _) => s.startMixerOutputStream(),
  'getMixerOutputWavHeader': (s, _, _) => s.getMixerOutputWavHeader(),
  'setBufferStream': (s, _, _) => s.setBufferStream(),
  'resetBufferStream': (s, a, _) => s.resetBufferStream(a),
  'getStreamTimeConsumed': (s, a, _) => s.getStreamTimeConsumed(a),
  'addAudioDataStream': (s, a, _) => s.addAudioDataStream(a, Uint8List(4)),
  'setDataIsEnded': (s, a, _) => s.setDataIsEnded(a),
  'getBufferSize': (s, a, _) => s.getBufferSize(a),
  'setPullBufferStream': (s, _, _) =>
      s.setPullBufferStream(audioSizeBytes: 100),
  'resetPullBufferStream': (s, a, _) => s.resetPullBufferStream(a),
  'addPullBufferDataStream': (s, a, _) =>
      s.addPullBufferDataStream(a, Uint8List(4)),
  'getPullBufferTimeRange': (s, a, _) => s.getPullBufferTimeRange(a),
  'loadAsset': (s, _, _) => s.loadAsset('missing.wav'),
  'loadUrl': (s, _, _) => s.loadUrl('http://localhost:1/x.wav'),
  'loadWaveform': (s, _, _) => s.loadWaveform(WaveForm.sin, false, 1, 0),
  'setWaveform': (s, a, _) => s.setWaveform(a, WaveForm.square),
  'setWaveformScale': (s, a, _) => s.setWaveformScale(a, 1),
  'setWaveformDetune': (s, a, _) => s.setWaveformDetune(a, 0),
  'setWaveformFreq': (s, a, _) => s.setWaveformFreq(a, 440),
  'setWaveformSuperWave': (s, a, _) => s.setWaveformSuperWave(a, true),
  'speechText': (s, _, _) => s.speechText('hi'),
  'play': (s, a, _) => s.play(a),
  'playClocked': (s, a, _) => s.playClocked(a, Duration.zero),
  'setDelaySamples': (s, _, h) => s.setDelaySamples(h, 0),
  'getStreamTime': (s, _, h) => s.getStreamTime(h),
  'resetStreamTime': (s, _, _) => s.resetStreamTime(),
  'getEngineTime': (s, _, _) => s.getEngineTime(),
  'getPlayheadTime': (s, _, _) => s.getPlayheadTime(),
  'getOutputLatency': (s, _, _) => s.getOutputLatency(),
  'getHardwareOutputLatency': (s, _, _) => s.getHardwareOutputLatency(),
  'playScheduled': (s, a, _) => s.playScheduled(a, Duration.zero),
  'stopScheduled': (s, _, h) => s.stopScheduled(h, Duration.zero),
  'fadeScheduled': (s, _, h) =>
      s.fadeScheduled(h, Duration.zero, 0, Duration.zero),
  'playSource': (s, _, _) => s.playSource(asset: 'missing.wav'),
  'pauseSwitch': (s, _, h) => s.pauseSwitch(h),
  'setPause': (s, _, h) => s.setPause(h, true),
  'getPause': (s, _, h) => s.getPause(h),
  'setRelativePlaySpeed': (s, _, h) => s.setRelativePlaySpeed(h, 1),
  'getRelativePlaySpeed': (s, _, h) => s.getRelativePlaySpeed(h),
  'getApproximateVolume': (s, _, _) => s.getApproximateVolume(0),
  'stop': (s, _, h) => s.stop(h),
  'stopAll': (s, _, _) => s.stopAll(),
  'stopAudioSource': (s, a, _) => s.stopAudioSource(a),
  'disposeSource': (s, a, _) => s.disposeSource(a),
  'disposeAllSources': (s, _, _) => s.disposeAllSources(),
  'getLooping': (s, _, h) => s.getLooping(h),
  'setLooping': (s, _, h) => s.setLooping(h, true),
  'getLoopPoint': (s, _, h) => s.getLoopPoint(h),
  'setLoopPoint': (s, _, h) => s.setLoopPoint(h, Duration.zero),
  'getLoopEndPoint': (s, _, h) => s.getLoopEndPoint(h),
  'setLoopEndPoint': (s, _, h) => s.setLoopEndPoint(h, null),
  'setVisualizationEnabled': (s, _, _) => s.setVisualizationEnabled(true),
  'getVisualizationEnabled': (s, _, _) => s.getVisualizationEnabled(),
  'getLength': (s, a, _) => s.getLength(a),
  'seek': (s, _, h) => s.seek(h, Duration.zero),
  'getPosition': (s, _, h) => s.getPosition(h),
  'getGlobalVolume': (s, _, _) => s.getGlobalVolume(),
  'setGlobalVolume': (s, _, _) => s.setGlobalVolume(0),
  'getVolume': (s, _, h) => s.getVolume(h),
  'setVolume': (s, _, h) => s.setVolume(h, 0),
  'getPan': (s, _, h) => s.getPan(h),
  'setPan': (s, _, h) => s.setPan(h, 0),
  'setPanAbsolute': (s, _, h) => s.setPanAbsolute(h, 0, 0),
  'getIsValidVoiceHandle': (s, _, h) => s.getIsValidVoiceHandle(h),
  'getActiveVoiceCount': (s, _, _) => s.getActiveVoiceCount(),
  'countAudioSource': (s, a, _) => s.countAudioSource(a),
  'getVoiceCount': (s, _, _) => s.getVoiceCount(),
  'getProtectVoice': (s, _, h) => s.getProtectVoice(h),
  'setProtectVoice': (s, _, h) => s.setProtectVoice(h, true),
  'setInaudibleBehavior': (s, _, h) => s.setInaudibleBehavior(h, true, false),
  'getMaxActiveVoiceCount': (s, _, _) => s.getMaxActiveVoiceCount(),
  'setMaxActiveVoiceCount': (s, _, _) => s.setMaxActiveVoiceCount(16),
  'setFftSmoothing': (s, _, _) => s.setFftSmoothing(0.5),
  'setFftDecibelRange': (s, _, _) => s.setFftDecibelRange(-100, 0),
  'fadeGlobalVolume': (s, _, _) => s.fadeGlobalVolume(0, Duration.zero),
  'fadeVolume': (s, _, h) => s.fadeVolume(h, 0, Duration.zero),
  'fadePan': (s, _, h) => s.fadePan(h, 0, Duration.zero),
  'fadeRelativePlaySpeed': (s, _, h) =>
      s.fadeRelativePlaySpeed(h, 1, Duration.zero),
  'schedulePause': (s, _, h) => s.schedulePause(h, Duration.zero),
  'scheduleStop': (s, _, h) => s.scheduleStop(h, Duration.zero),
  'oscillateVolume': (s, _, h) => s.oscillateVolume(h, 0, 1, Duration.zero),
  'oscillatePan': (s, _, h) => s.oscillatePan(h, 0, 1, Duration.zero),
  'oscillateRelativePlaySpeed': (s, _, h) =>
      s.oscillateRelativePlaySpeed(h, 1, 2, Duration.zero),
  'oscillateGlobalVolume': (s, _, _) =>
      s.oscillateGlobalVolume(0, 1, Duration.zero),
  'fadeGlobalFilterParameter': (s, _, _) =>
      s.fadeGlobalFilterParameter(FilterType.echoFilter, 0, 0, Duration.zero),
  'oscillateGlobalFilterParameter': (s, _, _) =>
      s.oscillateGlobalFilterParameter(
        FilterType.echoFilter,
        0,
        0,
        1,
        Duration.zero,
      ),
  'play3d': (s, a, _) => s.play3d(a, 0, 0, 0),
  'play3dClocked': (s, a, _) => s.play3dClocked(a, Duration.zero, 0, 0, 0),
  'play3dScheduled': (s, a, _) => s.play3dScheduled(a, Duration.zero, 0, 0, 0),
  'setRealtimeStream': (s, _, _) => s.setRealtimeStream(),
  'createCustomAudioSource': (s, _, _) =>
      s.createCustomAudioSource(webJsFunctionName: 'noop'),
  'setBufferIcyMetaInt': (s, a, _) => s.setBufferIcyMetaInt(a, 16000),
  'setMp3BufferIcyMetaInt': (s, a, _) => s.setMp3BufferIcyMetaInt(a, 16000),
  'destroyVoiceGroup': (s, _, h) => s.destroyVoiceGroup(h),
  'addVoicesToGroup': (s, _, h) => s.addVoicesToGroup(h, [h]),
  'isVoiceGroup': (s, _, h) => s.isVoiceGroup(h),
  'isVoiceGroupEmpty': (s, _, h) => s.isVoiceGroupEmpty(h),
  'createMixingBus': (s, _, _) => s.createMixingBus(),
  'set3dSoundSpeed': (s, _, _) => s.set3dSoundSpeed(343),
  'get3dSoundSpeed': (s, _, _) => s.get3dSoundSpeed(),
  'set3dListenerParameters': (s, _, _) =>
      s.set3dListenerParameters(0, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0),
  'set3dListenerPosition': (s, _, _) => s.set3dListenerPosition(0, 0, 0),
  'set3dListenerAt': (s, _, _) => s.set3dListenerAt(0, 0, -1),
  'set3dListenerUp': (s, _, _) => s.set3dListenerUp(0, 1, 0),
  'set3dListenerVelocity': (s, _, _) => s.set3dListenerVelocity(0, 0, 0),
  'set3dSourceParameters': (s, _, h) =>
      s.set3dSourceParameters(h, 0, 0, 0, 0, 0, 0),
  'set3dSourcePosition': (s, _, h) => s.set3dSourcePosition(h, 0, 0, 0),
  'set3dSourceVelocity': (s, _, h) => s.set3dSourceVelocity(h, 0, 0, 0),
  'set3dSourceMinMaxDistance': (s, _, h) =>
      s.set3dSourceMinMaxDistance(h, 1, 10),
  'set3dSourceAttenuation': (s, _, h) => s.set3dSourceAttenuation(h, 1, 1),
  'set3dSourceDopplerFactor': (s, _, h) => s.set3dSourceDopplerFactor(h, 1),
  'set3dDefaultMinMaxDistance': (s, a, _) =>
      s.set3dDefaultMinMaxDistance(a, 1, 10),
  'set3dDefaultAttenuation': (s, a, _) => s.set3dDefaultAttenuation(a, 1, 1),
  'set3dDefaultDopplerFactor': (s, a, _) => s.set3dDefaultDopplerFactor(a, 1),
  'set3dAutoUpdate': (s, _, _) => s.set3dAutoUpdate(true),
  'get3dAutoUpdate': (s, _, _) => s.get3dAutoUpdate(),
  'update3dAudio': (s, _, _) => s.update3dAudio(),
};

/// Members that, by design, do not throw SoLoudNotInitializedException.
const _exempt = {
  // State and streams, readable at any time.
  'get audioVisualizationEvents': 'a stream, readable at any time',
  'get isInitialized': 'the state itself',
  'get activeSounds': 'Dart-side list, empty before init',
  'get sampleRate': 'the last init value',
  'get audioDeviceStartFailures': 'a stream, readable at any time',
  'get isMixerOutputStreamRunning': 'false before init',
  'get minDecibels': 'the last set value',
  'get maxDecibels': 'the last set value',
  'get isRenderAheadEnabled': 'false before init',
  // Lifecycle.
  'init': 'starts the engine',
  'deinit': 'no-op before init',
  'deinitAsync': 'no-op before init',
  // Documented to work before init.
  'setLinuxAudioBackend': 'documented: before init it picks the backend',
  'setAudioDeviceIdleTimeout': 'documented: callable before init',
  'getAudioDeviceState': 'documented: safe before init (uninitialized)',
  'listPlaybackDevices': 'documented: safe before init',
  // Dart-side lookups and no-ops.
  'findAudioSourceByHandle': 'Dart-side lookup, null before init',
  'isValidAudioSource': 'Dart-side lookup, false before init',
  'stopMixerOutputStream': 'no-op without a capture',
  // Tools that decode a buffer without the engine.
  'readSamplesFromFile': 'decodes a file without the engine',
  'readSamplesFromMem': 'decodes a buffer without the engine',
  // Throw another exception.
  'createVoiceGroup': 'throws SoLoudCreateVoiceGroupDartException',
};

// ---------------------------------------------------------------------------
// Parsing lib/src/soloud.dart.

/// A public member of `class SoLoud`: `name`, `get name` or `set name`, with
/// its doc comment and its body.
typedef _Member = ({String doc, String body});

/// The public instance members of `class SoLoud` in [source].
Map<String, _Member> _publicMembers(String source) {
  // Blank out comments and string contents, keeping offsets, so braces in
  // them do not count. Doc comments are read from [source] afterwards.
  final code = source
      .replaceAllMapped(
        RegExp(r'/\*.*?\*/|//[^\n]*', dotAll: true),
        (m) => m[0]!.replaceAll(RegExp('[^\n]'), ' '),
      )
      .replaceAllMapped(
        RegExp(r"'(?:\\.|[^'\\\n])*'"),
        (m) => "'${' ' * (m[0]!.length - 2)}'",
      );
  final start = code.indexOf(RegExp(r'class SoLoud\s*\{'));
  final open = code.indexOf('{', start);
  final out = <String, _Member>{};
  var headerStart = open + 1;
  var depth = 0;
  var paren = 0;
  var bodyStart = -1;
  String? pending;
  var pendingStart = 0;

  void add(String header, int at, String body) {
    final name = _memberName(header);
    if (name == null) return;
    // The doc comment: the `///` lines right before the declaration.
    final before = source.substring(0, at).trimRight().split('\n');
    final doc = <String>[];
    for (final line in before.reversed) {
      final t = line.trim();
      if (t.startsWith('///')) {
        doc.insert(0, t);
      } else if (t.startsWith('@') || t.isEmpty && doc.isEmpty) {
        continue;
      } else {
        break;
      }
    }
    out[name] = (doc: doc.join('\n'), body: body);
  }

  for (var i = open + 1; i < code.length; i++) {
    final c = code[i];
    if (depth > 0) {
      if (c == '{') depth++;
      if (c == '}' && --depth == 0) {
        add(pending!, pendingStart, code.substring(bodyStart, i));
        headerStart = i + 1;
      }
      continue;
    }
    if (c == '(') paren++;
    if (c == ')') paren--;
    if (paren != 0) continue;
    if (c == '{') {
      pending = code.substring(headerStart, i);
      pendingStart = headerStart + pending.length - pending.trimLeft().length;
      depth = 1;
      bodyStart = i;
    } else if (c == ';') {
      final header = code.substring(headerStart, i);
      add(
        header,
        headerStart + header.length - header.trimLeft().length,
        header.contains('=>') ? header.substring(header.indexOf('=>')) : '',
      );
      headerStart = i + 1;
    } else if (c == '}') {
      break; // end of the class
    }
  }
  return out;
}

/// The member a declaration header declares, or null for fields,
/// constructors, static and private members.
String? _memberName(String header) {
  final t = header
      .replaceAll(RegExp(r'@\w+(\([^)]*\))?'), ' ')
      .replaceAll(RegExp(r'\s+'), ' ')
      .trim();
  if (t.isEmpty || t.startsWith('static ')) return null;
  final accessor = RegExp(r'\b(get|set) ([A-Za-z_]\w*)').firstMatch(t);
  if (accessor != null && !t.substring(0, accessor.start).contains('=')) {
    return accessor[2]!.startsWith('_')
        ? null
        : '${accessor[1]} ${accessor[2]}';
  }
  // A record return type such as `({...})` comes first: skip it.
  final rest = t.replaceFirst(RegExp(r'^\(\{[^}]*\}\)\s*'), '');
  final m = RegExp(r'([A-Za-z_]\w*)\s*(<[^()]*>)?\s*\(').firstMatch(rest);
  if (m == null) return null; // a field
  final before = rest.substring(0, m.start);
  if (before.contains('=') ||
      RegExp(r'\b(final|var|late)\b').hasMatch(before)) {
    return null; // a field initialized with a call
  }
  final name = m[1]!;
  if (name.startsWith('_') || name == 'SoLoud') return null;
  return name;
}

bool _checksInit(_Member m) =>
    m.body.contains('SoLoudNotInitializedException') ||
    // Delegates to the mixer output manager, which checks it.
    m.body.contains('_mixerOutputStreamManager.start') ||
    m.body.contains('_mixerOutputStreamManager.getWavHeader');

// ---------------------------------------------------------------------------

final _notInitialized = throwsA(isA<SoLoudNotInitializedException>());

/// Calls [call] and expects it to throw SoLoudNotInitializedException,
/// synchronously or from the returned future.
Future<void> _expectNotInitialized(
  String name,
  _Call call,
  AudioSource sound,
  SoundHandle handle,
) async {
  await expectLater(
    Future.sync(() => call(SoLoud.instance, sound, handle)),
    _notInitialized,
    reason: name,
  );
}

void main() {
  prepareEngineHost();
  final soloud = SoLoud.instance;
  tearDown(deinitTestEngine);

  final fakeSound = AudioSource(const SoundHash(12345));
  const fakeHandle = SoundHandle(0x0BAD0BAD);

  group('the member lists match lib/src/soloud.dart', () {
    const vmOnly = kIsWeb ? 'reads lib/src/soloud.dart, VM only' : null;
    late Map<String, _Member> members;
    setUpAll(() {
      if (!kIsWeb) {
        members = _publicMembers(File(_soloudPath).readAsStringSync());
      }
    });

    test('the class parses', skip: vmOnly, () {
      expect(members.length, greaterThan(100));
      expect(members.keys, containsAll(['play', 'get isInitialized', 'init']));
    });

    test('every public member is listed', skip: vmOnly, () {
      final listed = {..._calls.keys, ..._exempt.keys};
      final missing = members.keys.toSet().difference(listed).toList()..sort();
      expect(
        missing,
        isEmpty,
        reason:
            'New public members of SoLoud: add a call to _calls, or add them '
            'to _exempt with the reason they work without the engine.',
      );
      final stale = listed.difference(members.keys.toSet()).toList()..sort();
      expect(stale, isEmpty, reason: 'No longer public members of SoLoud.');
      expect(_calls.keys.toSet().intersection(_exempt.keys.toSet()), isEmpty);
    });

    test('members that check isInitialized are in _calls', skip: vmOnly, () {
      final wrong = [
        for (final MapEntry(:key, :value) in members.entries)
          if (_checksInit(value) != _calls.containsKey(key))
            '$key (checks init: ${_checksInit(value)})',
      ]..sort();
      expect(wrong, isEmpty);
    });

    test(
      'members documented to throw SoLoudNotInitializedException check it',
      skip: vmOnly,
      () {
        final wrong = [
          for (final MapEntry(:key, :value) in members.entries)
            // init() mentions the exception for the calls made before it.
            if (key != 'init' &&
                value.doc.contains('[SoLoudNotInitializedException]') &&
                !_checksInit(value))
              key,
        ]..sort();
        expect(wrong, isEmpty);
      },
    );
  });

  group('before init', () {
    for (final MapEntry(key: name, value: call) in _calls.entries) {
      test('$name throws SoLoudNotInitializedException', () async {
        expect(soloud.isInitialized, isFalse);
        await _expectNotInitialized(name, call, fakeSound, fakeHandle);
      });
    }

    test('exempt members work without the engine', () async {
      expect(soloud.activeSounds, isEmpty);
      expect(soloud.isMixerOutputStreamRunning, isFalse);
      expect(soloud.isRenderAheadEnabled, isFalse);
      expect(soloud.findAudioSourceByHandle(fakeHandle), isNull);
      expect(soloud.isValidAudioSource(fakeSound), isFalse);
      expect(soloud.getAudioDeviceState, returnsNormally);
      expect(soloud.stopMixerOutputStream, returnsNormally);
      expect(() => soloud.setAudioDeviceIdleTimeout(null), returnsNormally);
      soloud.setAudioDeviceIdleTimeout(const Duration(milliseconds: 500));
      if (!kIsWeb) await soloud.setLinuxAudioBackend(LinuxAudioBackend.auto);
      expect(soloud.listPlaybackDevices, returnsNormally);
      final samples = await soloud.readSamplesFromMem(wavBytes(), 64);
      expect(samples, isNotEmpty);
    });
  });

  group('after deinit', () {
    test('every listed member throws SoLoudNotInitializedException', () async {
      await initTestEngine(globalVolume: 0);
      final sound = await loadTestSound();
      final handle = soloud.play(sound, volume: 0);
      expect(soloud.getIsValidVoiceHandle(handle), isTrue);
      soloud.deinit();
      expect(soloud.isInitialized, isFalse);

      for (final MapEntry(key: name, value: call) in _calls.entries) {
        await _expectNotInitialized(name, call, sound, handle);
      }
    });

    test('after deinitAsync too', () async {
      await initTestEngine(globalVolume: 0);
      final sound = await loadTestWaveform();
      final handle = soloud.play(sound, volume: 0);
      await soloud.deinitAsync();
      for (final MapEntry(key: name, value: call) in _calls.entries) {
        await _expectNotInitialized(name, call, sound, handle);
      }
    });
  });
}

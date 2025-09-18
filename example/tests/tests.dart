import 'dart:async';
import 'dart:developer' as dev;
import 'dart:math';
import 'dart:ui';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/bindings/soloud_controller.dart';
import 'package:logging/logging.dart';

enum TestStatus {
  none,
  passed,
  failed,
  running,
}

/// A GUI for tests.
///
/// Run this with `flutter run tests/tests.dart`.
void main() {
  WidgetsFlutterBinding.ensureInitialized();

  runApp(
    MaterialApp(
      themeMode: ThemeMode.dark,
      darkTheme: ThemeData.dark(useMaterial3: true),
      scrollBehavior: const MaterialScrollBehavior().copyWith(
        // enable mouse dragging
        dragDevices: PointerDeviceKind.values.toSet(),
      ),
      home: const Padding(
        padding: EdgeInsets.all(8),
        child: MyHomePage(),
      ),
    ),
  );
}

class _Test {
  _Test({
    required this.name,
    required this.callback,
    // ignore: unused_element_parameter
    this.status = TestStatus.none,
  });
  final String name;
  final Future<StringBuffer> Function() callback;
  TestStatus status;
}

class MyHomePage extends StatefulWidget {
  const MyHomePage({super.key});

  @override
  State<MyHomePage> createState() => _MyHomePageState();
}

class _MyHomePageState extends State<MyHomePage> {
  final output = StringBuffer();
  final List<_Test> tests = [];
  final textEditingController = TextEditingController();

  @override
  void initState() {
    super.initState();

    /// Add all testing functions.
    tests.addAll([
      _Test(name: 'testProtectVoice', callback: testProtectVoice),
      _Test(
        name: 'testAllInstancesFinished',
        callback: testAllInstancesFinished,
      ),
      _Test(name: 'testCreateNotes', callback: testCreateNotes),
      _Test(name: 'testPlaySeekPause', callback: testPlaySeekPause),
      _Test(name: 'testPan', callback: testPan),
      _Test(name: 'testHandles', callback: testHandles),
      _Test(name: 'testStopFutures', callback: testStopFutures),
      _Test(name: 'loopingTests', callback: loopingTests),
      _Test(name: 'testSynchronousDeinit', callback: testSynchronousDeinit),
      _Test(name: 'testAsynchronousDeinit', callback: testAsynchronousDeinit),
      _Test(name: 'testVoiceGroups', callback: testVoiceGroups),
      _Test(name: 'testSoundFilters', callback: testSoundFilters),
      _Test(name: 'testGlobalFilters', callback: testGlobalFilters),
      _Test(name: 'testAsyncMultiLoad', callback: testAsyncMultiLoad),
    ]);
  }

  @override
  void dispose() {
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Scaffold(
        body: Column(
          children: [
            Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                OutlinedButton(
                  onPressed: () async {
                    for (var i = 0; i < tests.length; i++) {
                      await runTest(i);
                    }
                  },
                  child: const Text('Run All'),
                ),
                const SizedBox(height: 16),
                Wrap(
                  spacing: 8,
                  runSpacing: 8,
                  children: List.generate(
                    tests.length,
                    (index) {
                      return OutlinedButton(
                        style: ButtonStyle(
                          backgroundColor: switch (tests[index].status) {
                            TestStatus.failed =>
                              const WidgetStatePropertyAll(Colors.red),
                            TestStatus.passed =>
                              const WidgetStatePropertyAll(Colors.green),
                            TestStatus.running =>
                              const WidgetStatePropertyAll(Colors.yellow),
                            _ => null,
                          },
                        ),
                        onPressed: () async {
                          if (tests[index].status == TestStatus.running) {
                            return;
                          }
                          await runTest(index);
                        },
                        child: Text(
                          tests[index].name,
                        ),
                      );
                    },
                  ),
                ),
                const SizedBox(height: 16),
              ],
            ),
            Expanded(
              child: TextField(
                controller: textEditingController,
                style: const TextStyle(color: Colors.black, fontSize: 12),
                expands: true,
                maxLines: null,
                decoration: const InputDecoration(
                  fillColor: Colors.white,
                  filled: true,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  /// Run text with index [index].
  ///
  /// This outputs the asserts logs and the `StringBuffer` returned by
  /// the test functions.
  /// It also update the state of text buttons.
  Future<void> runTest(int index) async {
    tests[index].status = TestStatus.running;
    if (context.mounted) setState(() {});
    await runZonedGuarded<Future<void>>(
      () async {
        output
          ..write('===== RUNNING "${tests[index].name}" =====\n')
          ..write(await tests[index].callback())
          ..write('===== PASSED! =====\n\n')
          ..writeln();
        tests[index].status = TestStatus.passed;
        textEditingController.text = output.toString();
        debugPrint(output.toString());
        if (context.mounted) setState(() {});
      },
      (error, stack) {
        deinit();
        // if (error is SoLoudInitializationStoppedByDeinitException) {
        //   // This is to be expected in this test.
        //   return;
        // }
        output
          ..write('== TESTS "${tests[index].name}" FAILED with '
              'the following error(s) ==')
          ..writeln()
          ..writeAll([error, stack], '\n\n')
          ..writeln()
          ..writeln();
        // ignore: parameter_assignments
        tests[index].status = TestStatus.failed;
        textEditingController.text = output.toString();
        debugPrint(output.toString());
        if (context.mounted) setState(() {});
      },
    );
  }
}

// ////////////////////////////
// / Common methods
// ////////////////////////////

Future<void> initialize() async {
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);
}

void deinit() {
  SoLoud.instance.deinit();
}

Future<void> delay(int ms) async {
  await Future.delayed(Duration(milliseconds: ms), () {});
}

bool closeTo(num value, num expected, num epsilon) {
  return (value - expected).abs() <= epsilon.abs();
}

Future<AudioSource> loadAsset() async {
  return SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
}

// ///////////////////////////
// / Tests
// ///////////////////////////

/// Test setMaxActiveVoiceCount, setProtectedVoice and getProtectedVoice
Future<StringBuffer> testProtectVoice() async {
  await initialize();
  final defaultVoiceCount = SoLoud.instance.getMaxActiveVoiceCount();

  SoLoud.instance.setMaxActiveVoiceCount(6);
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == 6,
    "setMaxActiveVoiceCount() didn't work properly",
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    await SoLoud.instance.play(explosion);
    await delay(100);
  }

  /// play 1 protected [song]
  final songHandle = await SoLoud.instance.play(song);
  SoLoud.instance.setProtectVoice(songHandle, true);
  assert(
    SoLoud.instance.getProtectVoice(songHandle),
    "setProtectVoice() didn't work properly",
  );

  /// play 5 explosion
  for (var i = 0; i < 5; i++) {
    await SoLoud.instance.play(explosion);
    await delay(100);
  }

  await delay(1000);

  assert(
    SoLoud.instance.getIsValidVoiceHandle(songHandle) &&
        SoLoud.instance.getActiveVoiceCount() == 6,
    'The protected song has been stopped!',
  );

  deinit();

  /// Afer disposing the player and re-initializing, max active voices
  /// should be reset to 16
  await initialize();
  assert(
    SoLoud.instance.getMaxActiveVoiceCount() == defaultVoiceCount,
    'Max active voices are not reset to the default value after reinit!',
  );
  deinit();

  return StringBuffer();
}

/// Test allInstancesFinished stream
Future<StringBuffer> testAllInstancesFinished() async {
  final strBuf = StringBuffer();
  await initialize();

  await SoLoud.instance.disposeAllSources();
  assert(
    SoLoud.instance.activeSounds.isEmpty,
    'Active sounds even after disposeAllSound()',
  );

  final explosion =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');
  final song = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
    mode: LoadMode.disk,
  );

  // Set up unloading.
  var explosionDisposed = false;
  var songDisposed = false;
  unawaited(
    explosion.allInstancesFinished.first.then((_) async {
      strBuf.write('All instances of explosion finished.\n');
      await SoLoud.instance.disposeSource(explosion);
      explosionDisposed = true;
    }),
  );
  unawaited(
    song.allInstancesFinished.first.then((_) async {
      strBuf.write('All instances of song finished.\n');
      await SoLoud.instance.disposeSource(song);
      songDisposed = true;
    }),
  );

  await SoLoud.instance.play(explosion, volume: 0.2);
  final songHandle = await SoLoud.instance.play(song, volume: 0.6);
  await delay(500);
  await SoLoud.instance.play(explosion, volume: 0.3);

  // Let the second explosion play for its full duration.
  await delay(4000);

  await SoLoud.instance.stop(songHandle);
  await delay(1000);

  assert(explosionDisposed, "Explosion sound wasn't disposed.");
  assert(songDisposed, "Song sound wasn't disposed.");

  deinit();

  return strBuf;
}

/// Test waveform
Future<StringBuffer> testCreateNotes() async {
  await initialize();

  final notes0 = await SoLoudTools.createNotes(
    octave: 0,
  );
  final notes1 = await SoLoudTools.createNotes(
    octave: 1,
  );
  final notes2 = await SoLoudTools.createNotes(
    octave: 2,
  );
  assert(
    notes0.length == 12 && notes1.length == 12 && notes2.length == 12,
    'SoLoudTools.createNotes() failed!\n',
  );

  await SoLoud.instance.play(notes1[5]);
  await SoLoud.instance.play(notes2[0]);
  await delay(350);
  await SoLoud.instance.stop(notes1[5].handles.first);
  await SoLoud.instance.stop(notes2[0].handles.first);

  await SoLoud.instance.play(notes1[6]);
  await SoLoud.instance.play(notes2[1]);
  await delay(350);
  await SoLoud.instance.stop(notes1[6].handles.first);
  await SoLoud.instance.stop(notes2[1].handles.first);

  await SoLoud.instance.play(notes1[4]);
  await SoLoud.instance.play(notes1[11]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes1[11].handles.first);

  await SoLoud.instance.play(notes1[4]);
  await SoLoud.instance.play(notes0[9]);
  await delay(350);
  await SoLoud.instance.stop(notes1[4].handles.first);
  await SoLoud.instance.stop(notes0[9].handles.first);

  await SoLoud.instance.play(notes1[8]);
  await SoLoud.instance.play(notes1[1]);
  await delay(1500);
  await SoLoud.instance.stop(notes1[8].handles.first);
  await SoLoud.instance.stop(notes1[1].handles.first);

  deinit();

  return StringBuffer();
}

/// Test play, pause, seek, position
///
Future<StringBuffer> testPlaySeekPause() async {
  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  /// pause, seek test
  {
    await SoLoud.instance.play(currentSound);
    final length = SoLoud.instance.getLength(currentSound);
    assert(
      closeTo(length.inMilliseconds, 3773, 100),
      'getLength() failed: ${length.inMilliseconds}!\n',
    );
    await delay(1000);
    SoLoud.instance.pauseSwitch(currentSound.handles.first);
    final paused = SoLoud.instance.getPause(currentSound.handles.first);
    assert(paused, 'pauseSwitch() failed!');

    /// seek
    const wantedPosition = Duration(seconds: 2);
    SoLoud.instance.seek(currentSound.handles.first, wantedPosition);
    final position = SoLoud.instance.getPosition(currentSound.handles.first);
    assert(position == wantedPosition, 'getPosition() failed!');
  }

  deinit();
  return StringBuffer();
}

/// Test instancing playing handles and their disposal
Future<StringBuffer> testPan() async {
  /// Start audio isolate
  await initialize();

  final song =
      await SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3');

  final handle = await SoLoud.instance.play(song, volume: 0.5);

  SoLoud.instance.setPan(handle, -0.8);
  var pan = SoLoud.instance.getPan(handle);
  assert(closeTo(pan, -0.8, 0.00001), 'setPan() or getPan() failed!');

  await delay(1000);

  SoLoud.instance.setPan(handle, 0.8);
  pan = SoLoud.instance.getPan(handle);
  assert(closeTo(pan, 0.8, 0.00001), 'setPan() or getPan() failed!');
  await delay(1000);

  deinit();
  return StringBuffer();
}

/// Test instancing playing handles and their disposal
Future<StringBuffer> testHandles() async {
  var output = '';

  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  currentSound.soundEvents.listen((event) {
    if (event.event == SoundEventType.handleIsNoMoreValid) {
      output = 'SoundEvent.handleIsNoMoreValid';
    }
    if (event.event == SoundEventType.soundDisposed) {
      output = 'SoundEvent.soundDisposed';
    }
  });

  /// Play sample
  await SoLoud.instance.play(currentSound);
  assert(
    currentSound.soundHash.isValid && currentSound.handles.length == 1,
    'play() failed!',
  );

  /// 3798ms explosion.mp3 sample duration
  await delay(4500);
  assert(
    output == 'SoundEvent.handleIsNoMoreValid',
    'Sound end playback event not triggered!',
  );

  /// Play 4 sample
  await SoLoud.instance.play(currentSound);
  await SoLoud.instance.play(currentSound);
  await SoLoud.instance.play(currentSound);
  await SoLoud.instance.play(currentSound);
  assert(
    currentSound.handles.length == 4,
    'loadFromAssets() failed!',
  );

  /// Wait for the sample to finish and see in log:
  /// "SoundEvent.handleIsNoMoreValid .* has [3-2-1-0] active handles"
  /// 3798ms explosion.mp3 sample duration
  await delay(4500);
  assert(
    currentSound.handles.isEmpty,
    'Play 4 sample handles failed!',
  );

  deinit();
  return StringBuffer();
}

/// Test instancing playing handles and their disposal
Future<StringBuffer> testStopFutures() async {
  final output = StringBuffer();
  final severeLogs = <LogRecord>[];

  Logger.root.level = kDebugMode ? Level.FINE : Level.INFO;
  Logger.root.onRecord.listen((record) {
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );

    if (record.level > Level.INFO) {
      output.writeln(record.message);
      if (record.level >= Level.SEVERE) {
        severeLogs.add(record);
      }
    }
  });

  /// Start audio isolate
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');

  final random = Random(42);
  for (var i = 0; i < 50; i++) {
    final handle = await SoLoud.instance.play(currentSound);
    output.writeln('$handle started');
    await Future<void>.delayed(Duration(milliseconds: random.nextInt(5)));
    unawaited(
      SoLoud.instance
          .stop(handle)
          .then((_) => output.writeln('$handle stopped')),
    );
  }

  /// Wait a bit so that we can hear a sound to verify it's working as intended.
  await delay(SoLoud.stopSoundTimeout.inMilliseconds + 500);

  deinit();

  if (severeLogs.isNotEmpty) {
    throw Exception('Severe logs produced:\n'
        '${severeLogs.map((r) => '[${r.level}] ${r.message}').join('\n')}');
  }

  return output;
}

/// Test looping state and `loopingStartAt`
Future<StringBuffer> loopingTests() async {
  await initialize();

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  await SoLoud.instance.play(
    currentSound,
    looping: true,
    loopingStartAt: const Duration(seconds: 1),
  );
  assert(
    SoLoud.instance.getLooping(currentSound.handles.first),
    'looping failed!',
  );

  /// Wait for the first loop to start at 1s
  await delay(4100);
  assert(
    SoLoud.instance.getLoopPoint(currentSound.handles.first) ==
            const Duration(seconds: 1) &&
        SoLoud.instance.getPosition(currentSound.handles.first) >
            const Duration(seconds: 1),
    'looping start failed!',
  );

  deinit();
  return StringBuffer();
}

/// Test asynchronous `init()`-`deinit()`
Future<StringBuffer> testAsynchronousDeinit() async {
  /// test asynchronous init-deinit looping with a short decreasing time
  for (var t = 10; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    unawaited(
      SoLoud.instance.init().then(
        (_) {},
        onError: (Object e) {
          if (e is SoLoudInitializationStoppedByDeinitException) {
            // This is to be expected.
            debugPrint('$e\n');
            return;
          }
          debugPrint('TEST FAILED delay: $t. Player starting error: $e\n');
          error = e.toString();
        },
      ),
    );

    assert(error.isEmpty, error);

    /// wait for [t] ms and deinit()
    await delay(t);
    SoLoud.instance.deinit();
    final after = SoLoudController().soLoudFFI.isInited();

    assert(
      after == false,
      'TEST FAILED delay: $t. The player has not been deinited correctly!',
    );

    debugPrint('------------- awaited init delay $t passed\n');
  }
  return StringBuffer();
}

/// Test synchronous `init()`-`deinit()`
Future<StringBuffer> testSynchronousDeinit() async {
  /// test synchronous init-deinit looping with a short decreasing time
  /// waiting for `initialize()` to finish
  for (var t = 10; t >= 0; t--) {
    var error = '';

    /// Initialize the player
    await SoLoud.instance.init().then(
      (_) {},
      onError: (Object e) {
        if (e is SoLoudInitializationStoppedByDeinitException) {
          // This is to be expected.
          debugPrint('$e\n');
          return;
        }
        debugPrint('TEST FAILED delay: $t. Player starting error: $e');
        error = e.toString();
      },
    );
    assert(error.isEmpty, error);

    SoLoud.instance.deinit();

    assert(
      !SoLoud.instance.isInitialized ||
          !SoLoudController().soLoudFFI.isInited(),
      'ASSERT FAILED delay: $t. The player has not been '
      'inited or deinited correctly!',
    );

    debugPrint('------------- awaited init #$t passed\n');
  }

  /// Try init-play-deinit and again init-play without disposing the sound
  await SoLoud.instance.init();
  SoLoud.instance.setGlobalVolume(0.2);

  /// Load sample
  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  await SoLoud.instance.play(currentSound);
  await delay(100);
  await SoLoud.instance.play(currentSound);
  await delay(100);
  await SoLoud.instance.play(currentSound);

  await delay(2000);

  SoLoud.instance.deinit();

  /// Initialize again and check if the sound has been
  /// disposed correctly by `deinit()`
  await SoLoud.instance.init();
  assert(
    SoLoudController()
            .soLoudFFI
            .getIsValidVoiceHandle(currentSound.handles.first) ==
        false,
    'getIsValidVoiceHandle(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.countAudioSource(currentSound.soundHash) == 0,
    'getCountAudioSource(): sound not disposed by the engine',
  );
  assert(
    SoLoudController().soLoudFFI.getActiveVoiceCount() == 0,
    'getActiveVoiceCount(): sound not disposed by the engine',
  );
  SoLoud.instance.deinit();

  return StringBuffer();
}

/// Test voice groups.
Future<StringBuffer> testVoiceGroups() async {
  await initialize();

  final currentSound =
      await SoLoud.instance.loadAsset('assets/audio/explosion.mp3');

  /// Start playing sounds in pause state to get their handles.
  final h1 = await SoLoud.instance.play(currentSound, paused: true);
  final h2 = await SoLoud.instance.play(currentSound, paused: true);
  final h3 = await SoLoud.instance.play(currentSound, paused: true);
  final h4 = await SoLoud.instance.play(currentSound, paused: true);

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
  return StringBuffer();
}

/// Test sound filters.
Future<StringBuffer> testSoundFilters() async {
  final strBuf = StringBuffer();

  if (kIsWeb || kIsWasm) {
    return strBuf
      ..write('WARNING: Web does not support single sound filters.')
      ..writeln();
  }

  await initialize();

  final sound = await SoLoud.instance.loadAsset(
    'assets/audio/8_bit_mentality.mp3',
    // mode: LoadMode.disk,
  );

  final filter = sound.filters.echoFilter;

  /// Add filter to the sound.
  // ignore: cascade_invocations
  filter.activate();

  /// Set a handle filter. It must be set before it starts playing.
  final h1 = await SoLoud.instance.play(sound);

  /// Check if filter is active.
  assert(
    filter.isActive,
    'The filter has not been activate!',
  );

  /// Use the `Wet` attribute index.
  const value = 0.2;
  filter.wet(soundHandle: h1).value = value;
  final g = filter.wet(soundHandle: h1).value;
  assert(
    closeTo(g, value, 0.001),
    'Setting attribute to $value but obtained $g',
  );

  /// Oscillate wet parameter.
  filter.wet(soundHandle: h1).oscillateFilterParameter(
        from: 0.01,
        to: 2,
        time: const Duration(seconds: 2),
      );

  await delay(6000);

  /// Remove the filter.
  try {
    filter.deactivate();
  } on Exception catch (e) {
    strBuf
      ..write(e)
      ..writeln();
  }

  await SoLoud.instance.play(sound);

  /// Check if filter has been deactivated.
  assert(
    !filter.isActive,
    'The filter is still active after removing it!',
  );

  deinit();
  return strBuf;
}

/// Test global filters.
Future<StringBuffer> testGlobalFilters() async {
  final strBuf = StringBuffer();
  await initialize();

  late final AudioSource sound;
  try {
    sound = await SoLoud.instance.loadAsset(
      'assets/audio/8_bit_mentality.mp3',
      mode: LoadMode.disk,
    );
  } on Exception catch (e) {
    return strBuf
      ..write(e)
      ..writeln();
  }

  final filter = SoLoud.instance.filters.echoFilter;

  /// Add filter to the sound.
  // ignore: cascade_invocations
  filter.activate();

  await SoLoud.instance.play(sound);

  /// Check if filter is active.
  assert(
    filter.isActive,
    'The filter has not been activate!',
  );

  /// Use the `Wet` attribute index.
  const value = 0.2;
  filter.wet.value = value;
  final g = filter.wet.value;
  assert(
    closeTo(g, value, 0.001),
    'Setting attribute to $value but optained $g',
  );

  /// Oscillate wet parameter.
  filter.wet.oscillateFilterParameter(
    from: 0.01,
    to: 2,
    time: const Duration(seconds: 2),
  );

  await delay(6000);

  /// Remove the filter
  try {
    filter.deactivate();
  } on Exception catch (e) {
    strBuf
      ..write(e)
      ..writeln();
  }

  /// Check if filter has been deactivated.
  assert(
    !filter.isActive,
    'The filter has not been activate!',
  );

  deinit();
  return strBuf;
}

Future<StringBuffer> testAsyncMultiLoad() async {
  final strBuf = StringBuffer();
  await initialize();

  const prefix = 'assets/audio/12Bands/audiocheck.net_sin_';
  final sounds = [
    SoLoud.instance.loadAsset('${prefix}1000Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}125Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}16000Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}16Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}20000Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('assets/audio/8_bit_mentality.mp3'),
    SoLoud.instance.loadAsset('assets/audio/explosion.mp3'),
    SoLoud.instance.loadAsset('assets/audio/IveSeenThings.mp3'),
    SoLoud.instance.loadAsset('assets/audio/tic-1.wav'),
    SoLoud.instance.loadAsset('assets/audio/tic-2.wav'),
    SoLoud.instance.loadAsset('${prefix}2000Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}250Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}31.5Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}4000Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}500Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}63Hz_-3dBFS_2s.wav'),
    SoLoud.instance.loadAsset('${prefix}8000Hz_-3dBFS_2s.wav'),
  ];

  await Future.wait(sounds);
  // loading the same asset many times should not throw and just
  // display a warning.
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');
  await SoLoud.instance.loadAsset('assets/audio/tic-1.wav');

  deinit();
  return strBuf;
}

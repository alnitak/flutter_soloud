import 'dart:ffi';
import 'dart:io';
import 'dart:math';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Echo & Reverb VST3 GUI Randomized Lifecycle Test', () {
    late SoLoud soloud;
    late Vst3Host host;

    setUpAll(() async {
      TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
          .setMockMethodCallHandler(
            const MethodChannel('plugins.flutter.io/path_provider'),
            (MethodCall methodCall) async => Directory.systemTemp.path,
          );
      soloud = SoLoud.instance;
      await soloud.init(sampleRate: 48000, bufferSize: 512);
      host = Vst3Host();
    });

    tearDownAll(() async {
      if (soloud.isInitialized) {
        soloud.deinit();
      }
    });

    test('Randomly loads Echo/Reverb, toggles sound, opens/closes GUI', () async {
      String echoPath;
      String reverbPath;

      if (Platform.isWindows) {
        final localApp = Platform.environment['LOCALAPPDATA'];
        final userProfile = Platform.environment['USERPROFILE'];
        final base = (localApp != null && localApp.isNotEmpty)
            ? localApp
            : (userProfile != null && userProfile.isNotEmpty)
            ? '$userProfile\\AppData\\Local'
            : r'C:\Users\Default\AppData\Local';
        final p = '$base\\Programs\\Common\\VST3\\echo.vst3';
        const winBuilt =
            r'c:\workspace\flutter_vst3\vsts\echo\build\VST3\Release\echo.vst3';
        echoPath = (Directory(p).existsSync() || File(p).existsSync())
            ? p
            : winBuilt;

        final rp = '$base\\Programs\\Common\\VST3\\flutter_reverb.vst3';
        const winReverb =
            r'c:\workspace\flutter_vst3\vsts\flutter_reverb\build\VST3\Release\flutter_reverb.vst3';
        reverbPath = (Directory(rp).existsSync() || File(rp).existsSync())
            ? rp
            : winReverb;
      } else if (Platform.isLinux) {
        final home = Platform.environment['HOME'] ?? '';
        final p = '$home/.vst3/echo.vst3';
        const linuxBuilt =
            '/home/deimos/FLUTTER/libs/flutter_vst3/vsts/echo/build/VST3/Release/echo.vst3';
        echoPath = (Directory(p).existsSync() || File(p).existsSync())
            ? p
            : linuxBuilt;

        final rp = '$home/.vst3/flutter_reverb.vst3';
        const linuxReverb =
            '/home/deimos/FLUTTER/libs/flutter_vst3/vsts/flutter_reverb/build/VST3/Release/flutter_reverb.vst3';
        reverbPath = (Directory(rp).existsSync() || File(rp).existsSync())
            ? rp
            : linuxReverb;
      } else {
        echoPath =
            '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/echo.vst3';
        reverbPath =
            '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/flutter_reverb.vst3';
      }

      if (!host.isSupported) {
        print('VST3 hosting is not supported in this test build.');
        return;
      }

      print('=== VST3 Randomized Test Starting ===');
      print(
        'Echo path: $echoPath (exists: ${Directory(echoPath).existsSync()})',
      );
      print(
        'Reverb path: $reverbPath (exists: ${Directory(reverbPath).existsSync()})',
      );

      final echoClasses = host.scanBundle(echoPath);
      expect(
        echoClasses,
        isNotEmpty,
        reason: 'Failed scanning Echo: $echoPath',
      );
      final echoUid = echoClasses.first.classUid;

      final reverbClasses = host.scanBundle(reverbPath);
      expect(
        reverbClasses,
        isNotEmpty,
        reason: 'Failed scanning Reverb: $reverbPath',
      );
      final reverbUid = reverbClasses.first.classUid;

      final wave = await soloud.loadWaveform(WaveForm.sin, true, 0.25, 1.0);
      SoundHandle? playHandle;
      Vst3EffectPlugin? currentEffect;
      String? currentName;
      bool guiOpen = false;
      final slot = soloud.filters.customFilters[0];

      Future<void> pumpEvents(int ms) async {
        if (Platform.isWindows) {
          final user32 = DynamicLibrary.open('user32.dll');
          final peekMessage = user32
              .lookupFunction<
                Int32 Function(Pointer<Uint8>, IntPtr, Uint32, Uint32, Uint32),
                int Function(Pointer<Uint8>, int, int, int, int)
              >('PeekMessageW');
          final translateMessage = user32
              .lookupFunction<
                Int32 Function(Pointer<Uint8>),
                int Function(Pointer<Uint8>)
              >('TranslateMessage');
          final dispatchMessage = user32
              .lookupFunction<
                IntPtr Function(Pointer<Uint8>),
                int Function(Pointer<Uint8>)
              >('DispatchMessageW');

          final msg = calloc<Uint8>(64);
          final sw = Stopwatch()..start();
          while (sw.elapsedMilliseconds < ms) {
            while (peekMessage(msg, 0, 0, 0, 1) != 0) {
              translateMessage(msg);
              dispatchMessage(msg);
            }
            await Future<void>.delayed(const Duration(milliseconds: 16));
          }
          calloc.free(msg);
        } else if (Platform.isLinux) {
          try {
            final glib = DynamicLibrary.open('libglib-2.0.so.0');
            final gMainContextIteration = glib
                .lookupFunction<
                  Int32 Function(Pointer<Void>, Int32),
                  int Function(Pointer<Void>, int)
                >('g_main_context_iteration');
            final sw = Stopwatch()..start();
            while (sw.elapsedMilliseconds < ms) {
              while (gMainContextIteration(nullptr, 0) != 0) {}
              await Future<void>.delayed(const Duration(milliseconds: 16));
            }
          } catch (_) {
            await Future<void>.delayed(Duration(milliseconds: ms));
          }
        } else {
          await Future<void>.delayed(Duration(milliseconds: ms));
        }
      }

      final random = Random(42);
      const totalSteps = 25;

      print('\n--- Starting 25 randomized stress test operations ---\n');

      for (int step = 1; step <= totalSteps; ++step) {
        // Collect currently possible actions
        final availableActions = <String>[];

        // Sound actions
        if (playHandle == null) {
          availableActions.add('START_SOUND');
        } else {
          availableActions.add('STOP_SOUND');
        }

        // Plugin actions
        if (currentEffect == null) {
          availableActions.add('LOAD_ECHO');
          availableActions.add('LOAD_REVERB');
        } else {
          if (!guiOpen) {
            availableActions.add('OPEN_GUI');
            availableActions.add('UNLOAD_PLUGIN');
          } else {
            availableActions.add('CLOSE_GUI');
            availableActions.add(
              'UNLOAD_PLUGIN',
            ); // tests unloading while GUI is open!
          }
        }

        final action =
            availableActions[random.nextInt(availableActions.length)];
        print(
          'Step $step/$totalSteps: [$action] '
          '(Sound: ${playHandle != null ? "PLAYING" : "STOPPED"}, '
          'Plugin: ${currentName ?? "NONE"}, '
          'GUI: ${guiOpen ? "OPEN" : "CLOSED"})',
        );

        switch (action) {
          case 'START_SOUND':
            playHandle = soloud.play(wave, volume: 0.1, looping: true);
            print('  -> Sound started (handle: $playHandle)');
            await pumpEvents(200);
            break;

          case 'STOP_SOUND':
            if (playHandle != null) {
              soloud.stop(playHandle);
              playHandle = null;
              print('  -> Sound stopped');
            }
            await pumpEvents(200);
            break;

          case 'LOAD_ECHO':
            currentEffect = host.loadEffect(
              bundlePath: echoPath,
              classUid: echoUid,
              slot: slot,
            );
            currentName = 'Echo';
            print('  -> Echo loaded (handle: ${currentEffect.pluginHandle})');
            await pumpEvents(200);
            break;

          case 'LOAD_REVERB':
            currentEffect = host.loadEffect(
              bundlePath: reverbPath,
              classUid: reverbUid,
              slot: slot,
            );
            currentName = 'Reverb';
            print('  -> Reverb loaded (handle: ${currentEffect.pluginHandle})');
            await pumpEvents(200);
            break;

          case 'OPEN_GUI':
            if (currentEffect != null && !guiOpen) {
              final ok = currentEffect.openEditorWindow(
                title: '$currentName VST3 GUI',
              );
              expect(ok, isTrue, reason: 'Failed opening $currentName GUI');
              guiOpen = true;
              print('  -> $currentName GUI opened');
              // Give user visual time to see the GUI
              await pumpEvents(2500);
            }
            break;

          case 'CLOSE_GUI':
            if (currentEffect != null && guiOpen) {
              currentEffect.closeEditorWindow();
              guiOpen = false;
              print('  -> $currentName GUI closed');
              await pumpEvents(300);
            }
            break;

          case 'UNLOAD_PLUGIN':
            if (currentEffect != null) {
              if (guiOpen) {
                currentEffect.closeEditorWindow();
                guiOpen = false;
                print(
                  '  -> Automatically closed GUI before disposing $currentName',
                );
              }
              currentEffect.dispose();
              print('  -> $currentName disposed');
              currentEffect = null;
              currentName = null;
              await pumpEvents(300);
            }
            break;
        }
      }

      print(
        '\n--- Completed all randomized steps! Cleaning up remaining state ---',
      );

      // Final cleanup
      if (guiOpen && currentEffect != null) {
        currentEffect.closeEditorWindow();
        guiOpen = false;
        print('Closed leftover GUI window.');
      }
      if (currentEffect != null) {
        currentEffect.dispose();
        currentEffect = null;
        print('Disposed leftover plugin effect.');
      }
      if (playHandle != null) {
        soloud.stop(playHandle);
        playHandle = null;
        print('Stopped sound.');
      }
      await soloud.disposeSource(wave);
      print('=== Randomized VST3 Test Finished Successfully! ===\n');
    });
  });
}

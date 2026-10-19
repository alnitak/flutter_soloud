import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

bool _bundleExists(String path) =>
    Directory(path).existsSync() || File(path).existsSync();

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('VST3 Plugin Hosting Tests (Real Plugins)', () {
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

    test('Reports standard search paths on desktop', () {
      if (Platform.isMacOS || Platform.isWindows || Platform.isLinux) {
        expect(host.standardSearchPaths, isNotEmpty);
      }
    });

    test(
      'Loads and processes audio effect (flutter_reverb / Surge XT Effects)',
      () async {
        const userReverb =
            '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/flutter_reverb.vst3';
        const userEcho =
            '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/echo.vst3';
        const systemFx = '/Library/Audio/Plug-Ins/VST3/Surge XT Effects.vst3';
        const winBuiltReverb =
            r'c:\workspace\flutter_vst3\vsts\flutter_reverb\build\VST3\Release\flutter_reverb.vst3';
        const winBuiltEcho =
            r'c:\workspace\flutter_vst3\vsts\echo\build\VST3\Release\echo.vst3';
        final localApp = Platform.environment['LOCALAPPDATA'];
        final winUserReverb = localApp != null
            ? '$localApp\\Programs\\Common\\VST3\\flutter_reverb.vst3'
            : '';

        final candidatePaths = [
          if (_bundleExists(userReverb)) userReverb,
          if (_bundleExists(userEcho)) userEcho,
          if (_bundleExists(systemFx)) systemFx,
          if (_bundleExists(winBuiltReverb)) winBuiltReverb,
          if (_bundleExists(winBuiltEcho)) winBuiltEcho,
          if (winUserReverb.isNotEmpty && _bundleExists(winUserReverb)) winUserReverb,
        ];

        if (candidatePaths.isEmpty || !host.isSupported) {
          return;
        }

        for (final effectPath in candidatePaths) {
          final classes = host.scanBundle(effectPath);
          expect(classes, isNotEmpty, reason: 'Failed scanning $effectPath');
          expect(classes.first.isEffect, isTrue);

          final slot = soloud.filters.customFilters[0];
          final effect = host.loadEffect(
            bundlePath: effectPath,
            classUid: classes.first.classUid,
            slot: slot,
          );

          expect(effect.pluginHandle, greaterThan(0));
          expect(effect.hasEditor, isA<bool>());

          if (effect.hasEditor) {
            final opened = effect.openEditorWindow(title: 'Test GUI');
            expect(opened, isTrue);
            await Future<void>.delayed(const Duration(milliseconds: 50));
            effect.closeEditorWindow();
          }

          if (effect.parameters.isNotEmpty) {
            final firstParam = effect.parameters.first;
            effect.setParameterNormalized(firstParam.paramId, 0.75);
            final readBack = effect.getParameterNormalized(firstParam.paramId);
            expect(readBack, closeTo(0.75, 0.1));
            final display = effect.getParameterDisplay(firstParam.paramId);
            expect(display, isA<String>());

            effect
                .mapToSlotParameter(
                  slotParamIndex: 1,
                  vst3ParamId: firstParam.paramId,
                )
                .fadeFilterParameter(
                  to: 0.5,
                  time: const Duration(milliseconds: 50),
                );

            expect(effect.onRestartComponent, isNotNull);
            final refreshed = effect.refreshParameters();
            expect(refreshed.length, equals(effect.parameters.length));

            // Start audio playback so the real-time processor processes audio
            final wave = await soloud.loadWaveform(WaveForm.sin, true, 0.25, 0);
            final handle = soloud.play(wave, volume: 0.05);

            final fxTypeParam = effect.parameters.where(
              (p) => p.title == 'FX Type',
            ).firstOrNull;
            if (fxTypeParam != null) {
              effect.setParameterNormalized(fxTypeParam.paramId, 0.3);
              await Future<void>.delayed(const Duration(milliseconds: 100));
              final updatedParams = effect.refreshParameters();
              expect(updatedParams, isNotEmpty);
            }

            await soloud.stop(handle);
            await soloud.disposeSource(wave);
          }

          // Test preset state save & restore
          final state = effect.saveState();
          expect(state, isNotEmpty);
          effect
            ..restoreState(state)
            ..dispose();
          expect(effect.isDisposed, isTrue);
        }
      },
    );

    test(
      'Loads and generates sound from instrument synth (Surge XT)',
      () async {
        const synthPath = '/Library/Audio/Plug-Ins/VST3/Surge XT.vst3';

        if (!_bundleExists(synthPath) || !host.isSupported) {
          return;
        }

        final classes = host.scanBundle(synthPath);
        expect(classes, isNotEmpty);
        final synthClass = classes.firstWhere(
          (c) => c.isInstrument,
          orElse: () => classes.first,
        );

        final synth = host.loadInstrument(
          bundlePath: synthPath,
          classUid: synthClass.classUid,
        );

        expect(synth.pluginHandle, greaterThan(0));
        expect(synth.hasEditor, isTrue);

        // Play as voice and trigger MIDI note
        final voice = soloud.play(synth.audioSource, volume: 0.05);
        expect(voice.id, isNonZero);

        synth.noteOn(pitch: 60, velocity: 0.9);
        await Future<void>.delayed(const Duration(milliseconds: 100));
        synth.noteOff(pitch: 60);
        await Future<void>.delayed(const Duration(milliseconds: 40));

        final state = synth.saveState();
        expect(state, isNotEmpty);
        synth.restoreState(state);

        await soloud.stop(voice);
        synth.dispose();
        expect(synth.isDisposed, isTrue);
      },
    );
  });
}

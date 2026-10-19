import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter/services.dart';
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('Echo VST3 GUI Test', () {
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

    test('Loads echo.vst3 and opens its GUI window', () async {
      String targetPath;
      if (Platform.isWindows) {
        final localApp = Platform.environment['LOCALAPPDATA'];
        final userProfile = Platform.environment['USERPROFILE'];
        final base = (localApp != null && localApp.isNotEmpty)
            ? localApp
            : (userProfile != null && userProfile.isNotEmpty)
            ? '$userProfile\\AppData\\Local'
            : r'C:\Users\Default\AppData\Local';
        final echoPath = '$base\\Programs\\Common\\VST3\\echo.vst3';
        const winBuiltEcho =
            r'c:\workspace\flutter_vst3\vsts\echo\build\VST3\Release\echo.vst3';
        targetPath =
            (Directory(echoPath).existsSync() || File(echoPath).existsSync())
            ? echoPath
            : winBuiltEcho;
      } else if (Platform.isLinux) {
        final home = Platform.environment['HOME'] ?? '';
        final echoLinux = '$home/.vst3/echo.vst3';
        const linuxBuiltEcho =
            '/home/deimos/FLUTTER/libs/flutter_vst3/vsts/echo/build/VST3/Release/echo.vst3';
        targetPath =
            (Directory(echoLinux).existsSync() || File(echoLinux).existsSync())
            ? echoLinux
            : linuxBuiltEcho;
      } else {
        const echoMac =
            '/Volumes/NVME/Users/deimos/Library/Audio/Plug-Ins/VST3/echo.vst3';
        targetPath = echoMac;
      }

      if (!host.isSupported) {
        print('VST3 hosting is not supported in this test build (requires enable_vst3).');
        return;
      }

      final classes = host.scanBundle(targetPath);
      expect(classes, isNotEmpty, reason: 'Failed scanning $targetPath');
      final classUid = classes.first.classUid;

      final slot = soloud.filters.customFilters[0];
      final effect = host.loadEffect(
        bundlePath: targetPath,
        classUid: classes.first.classUid,
        slot: slot,
      );

      expect(effect.pluginHandle, greaterThan(0));
      expect(effect.hasEditor, isTrue);

      print('Opening GUI for ${classes.first.name}...');
      final opened = effect.openEditorWindow(title: 'Echo GUI Test Window');
      expect(opened, isTrue);

      print('GUI opened successfully, pumping Windows messages for 10 seconds...');
      if (Platform.isWindows) {
        final user32 = DynamicLibrary.open('user32.dll');
        final peekMessage = user32.lookupFunction<
            Int32 Function(Pointer<Uint8>, IntPtr, Uint32, Uint32, Uint32),
            int Function(Pointer<Uint8>, int, int, int, int)>('PeekMessageW');
        final translateMessage = user32.lookupFunction<
            Int32 Function(Pointer<Uint8>),
            int Function(Pointer<Uint8>)>('TranslateMessage');
        final dispatchMessage = user32.lookupFunction<
            IntPtr Function(Pointer<Uint8>),
            int Function(Pointer<Uint8>)>('DispatchMessageW');

        // MSG structure is 48 bytes on x64
        final msg = calloc<Uint8>(64);
        final sw = Stopwatch()..start();
        while (sw.elapsed < const Duration(seconds: 10)) {
          while (peekMessage(msg, 0, 0, 0, 1 /* PM_REMOVE */) != 0) {
            translateMessage(msg);
            dispatchMessage(msg);
          }
          await Future<void>.delayed(const Duration(milliseconds: 16));
        }
        calloc.free(msg);
      } else {
        await Future<void>.delayed(const Duration(seconds: 10));
      }

      print('Closing GUI...');
      effect.closeEditorWindow();
      print('GUI closed cleanly.');

      print('Reopening GUI for ${classes.first.name}...');
      final reopened = effect.openEditorWindow(title: 'Echo GUI Test Window (Reopened)');
      expect(reopened, isTrue);
      print('GUI reopened successfully, waiting 1 second...');
      await Future<void>.delayed(const Duration(seconds: 1));

      print('Closing reopened GUI...');
      effect.closeEditorWindow();
      print('Reopened GUI closed cleanly.');
    });
  });
}

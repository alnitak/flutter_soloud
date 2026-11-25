// Mixing buses with the real engine: lifecycle, voices played through a bus,
// a bus nested in another one (annexing its voice), filters on a bus, and
// using a bus that has been disposed. Levels are measured on the captured
// mixer output.
@Tags(['device'])
library;

import 'package:flutter/foundation.dart' show kIsWeb;
import 'package:flutter_soloud/flutter_soloud.dart';
import 'package:flutter_soloud/src/filters/biquad_resonant_filter.dart';
import 'package:flutter_test/flutter_test.dart';

import '../support/mixer_capture.dart';
import '../support/sounds.dart';

void main() {
  setUpEngineTests(globalVolume: 0.05);
  final soloud = SoLoud.instance;

  late AudioSource tone;
  final created = <Bus>[];

  setUp(() async => tone = await loadTestWaveform());
  tearDown(() {
    for (final bus in created) {
      if (Buses().buses.contains(bus)) bus.dispose();
    }
    created.clear();
  });

  Bus newBus([String name = '']) {
    final bus = soloud.createMixingBus(name: name);
    created.add(bus);
    return bus;
  }

  double level(CapturedOutput out) => out.rms(0);

  group('lifecycle', () {
    test('a new bus has an id, is registered and is not active', () {
      final bus = newBus('sfx');
      expect(bus.busId, greaterThan(0));
      expect(bus.isActive, isFalse);
      expect(bus.getActiveVoiceCount(), 0);
      expect(Buses().buses, contains(bus));
      expect(Buses().byName('sfx'), same(bus));
      expect(Buses().byId(bus.busId), same(bus));
    });

    test('each bus gets its own id', () {
      final a = newBus();
      final b = newBus();
      expect(a.busId, isNot(b.busId));
      expect(a, isNot(b));
    });

    test('playOnEngine gives the bus a voice', () {
      final bus = newBus();
      final h = bus.playOnEngine(volume: 0.7);
      expect(h.isError, isFalse);
      expect(bus.isActive, isTrue);
      expect(bus.soundHandle, h);
      expect(soloud.getIsValidVoiceHandle(h), isTrue);
      expect(soloud.getVolume(h), closeTo(0.7, 1e-6));
    });

    test('playOnEngine(paused: true) starts the bus paused', () {
      final bus = newBus();
      final h = bus.playOnEngine(paused: true);
      expect(soloud.getPause(h), isTrue);
    });

    test('a bus that is not playing cannot play voices', () {
      final bus = newBus();
      expect(
        () => bus.play(tone),
        throwsA(isA<SoLoudFailedToStartPlaybackCppException>()),
      );
    });

    test('dispose unregisters the bus and stops its voices', () {
      final bus = newBus('gone');
      final busHandle = bus.playOnEngine();
      final voice = bus.play(tone);
      bus.dispose();
      expect(Buses().buses, isNot(contains(bus)));
      expect(Buses().buses.where((b) => b.name == 'gone'), isEmpty);
      expect(soloud.getIsValidVoiceHandle(busHandle), isFalse);
      expect(soloud.getIsValidVoiceHandle(voice), isFalse);
      expect(bus.isActive, isFalse);
    });

    test(
      'a bus from before a deinit does not drive a bus of the next engine',
      () async {
        final stale = newBus()..playOnEngine();
        soloud.deinit();
        await initTestEngine(globalVolume: 0.05);
        // The new engine numbers its buses from 1 again.
        final fresh = [newBus(), newBus(), newBus()];
        expect(Buses().buses, isNot(contains(stale)));
        expect(fresh.map((b) => b.busId), contains(stale.busId));
        expect(
          stale.playOnEngine,
          throwsA(isA<SoLoudBusDisposedDartException>()),
        );
      },
    );
  });

  group('voices on a bus', () {
    test('are counted by the bus', () async {
      final bus = newBus()..playOnEngine();
      final a = bus.play(tone);
      final b = bus.play(tone, volume: 0.4);
      expect(bus.getActiveVoiceCount(), 2);
      expect(soloud.getVolume(b), closeTo(0.4, 1e-6));
      await soloud.stop(a);
      expect(bus.getActiveVoiceCount(), 1);
    });

    test('are audible through the bus', () async {
      newBus()
        ..playOnEngine()
        ..play(tone);
      expect(level(await captureOutput()), greaterThan(0.005));
    });

    test('follow the volume of the bus', () async {
      final bus = newBus();
      final busHandle = bus.playOnEngine();
      bus.play(tone);
      final full = level(await captureOutput());
      soloud.setVolume(busHandle, 0.5);
      final half = level(await captureOutput());
      soloud.setVolume(busHandle, 0);
      final muted = level(await captureOutput());
      expect(half / full, closeTo(0.5, 0.03));
      expect(muted, lessThan(1e-5));
    });

    test('stop being mixed while the bus is paused', () async {
      final bus = newBus();
      final busHandle = bus.playOnEngine();
      final voice = bus.play(tone);
      // A voice on the engine keeps the device running.
      soloud
        ..play(await loadSilence(), looping: true)
        ..setPause(busHandle, true);
      expect(level(await captureOutput()), lessThan(1e-5));
      expect(soloud.getIsValidVoiceHandle(voice), isTrue);
      soloud.setPause(busHandle, false);
      expect(level(await captureOutput()), greaterThan(0.005));
    });

    test('annexSound moves an engine voice into the bus', () async {
      final bus = newBus();
      final busHandle = bus.playOnEngine();
      final voice = soloud.play(tone);
      bus.annexSound(voice);
      expect(bus.getActiveVoiceCount(), 1);
      soloud.setVolume(busHandle, 0);
      expect(level(await captureOutput()), lessThan(1e-5));
    });

    test('annexSound into a bus that is not playing keeps the voice', () {
      final bus = newBus();
      final voice = soloud.play(tone);
      bus.annexSound(voice);
      expect(soloud.getIsValidVoiceHandle(voice), isTrue);
      expect(bus.getActiveVoiceCount(), 0);
    });
  });

  group('nested buses', () {
    late Bus outer;
    late Bus inner;
    late SoundHandle outerHandle;
    late SoundHandle innerHandle;

    setUp(() {
      outer = newBus('outer');
      inner = newBus('inner');
      outerHandle = outer.playOnEngine();
      innerHandle = inner.playOnEngine();
      outer.annexSound(innerHandle);
    });

    test('the inner bus is a voice of the outer one', () {
      expect(outer.getActiveVoiceCount(), 1);
      inner.play(tone);
      expect(inner.getActiveVoiceCount(), 1);
      expect(outer.getActiveVoiceCount(), 1);
    });

    test('a voice on the inner bus is audible', () async {
      inner.play(tone);
      expect(level(await captureOutput()), greaterThan(0.005));
    });

    test('the outer volume scales the inner bus', () async {
      inner.play(tone);
      final full = level(await captureOutput());
      soloud.setVolume(outerHandle, 0.5);
      final half = level(await captureOutput());
      soloud.setVolume(outerHandle, 0);
      final muted = level(await captureOutput());
      expect(half / full, closeTo(0.5, 0.03));
      expect(muted, lessThan(1e-5));
    });

    test('pausing the outer bus silences the inner one', () async {
      inner.play(tone);
      soloud
        ..play(await loadSilence(), looping: true)
        ..setPause(outerHandle, true);
      expect(level(await captureOutput()), lessThan(1e-5));
    });

    test('disposing the outer bus stops the inner one', () {
      final voice = inner.play(tone);
      outer.dispose();
      expect(soloud.getIsValidVoiceHandle(innerHandle), isFalse);
      expect(soloud.getIsValidVoiceHandle(voice), isFalse);
      // The inner Bus object is still registered, but has no voice.
      expect(Buses().buses, contains(inner));
    });
  });

  group('filters on a bus', () {
    test('activate and deactivate before and after playOnEngine', () {
      final bus = newBus();
      final echo = bus.filters.echoFilter..activate();
      expect(echo.isActive, isTrue);
      expect(soloud.filters.echoFilter.isActive, isFalse);
      bus.playOnEngine();
      expect(echo.isActive, isTrue);
      echo.deactivate();
      expect(echo.isActive, isFalse);
    });

    test('activating twice throws SoLoudFilterAlreadyAddedException', () {
      final bus = newBus();
      final lofi = bus.filters.lofiFilter..activate();
      expect(lofi.activate, throwsA(isA<SoLoudFilterAlreadyAddedException>()));
      lofi.deactivate();
      expect(lofi.deactivate, throwsA(isA<SoLoudFilterNotFoundException>()));
    });

    test('parameters need the bus to be playing', () {
      final bus = newBus();
      final echo = bus.filters.echoFilter..activate();
      expect(
        () => echo.wet().value,
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
      expect(
        () => echo.wet().value = 0.5,
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
      expect(
        () => echo.wet().fadeFilterParameter(to: 0, time: Duration.zero),
        throwsA(isA<SoLoudSoundHandleNotFoundCppException>()),
      );
    });

    test('parameters round-trip on a playing bus', () {
      final bus = newBus();
      final busHandle = bus.playOnEngine();
      final echo = bus.filters.echoFilter..activate();
      expect(echo.delay().value, closeTo(0.3, 1e-6));
      echo.delay().value = 0.8;
      expect(echo.delay().value, closeTo(0.8, 1e-6));
      // Read through the bus's voice: a single-sound filter, not on the web.
      if (!kIsWeb) {
        expect(echo.delay(soundHandle: busHandle).value, closeTo(0.8, 1e-6));
      }
      // The global echo is a different filter.
      expect(soloud.filters.echoFilter.isActive, isFalse);
    });

    test('fadeFilterParameter on a bus reaches the target', () async {
      final bus = newBus()
        ..playOnEngine()
        ..play(tone);
      final echo = bus.filters.echoFilter..activate();
      echo.wet().fadeFilterParameter(
        to: 0.25,
        time: const Duration(milliseconds: 100),
      );
      expect(
        await waitUntil(() => (echo.wet().value - 0.25).abs() < 1e-3),
        isTrue,
      );
    });

    test('a low-pass on the bus filters its voices only', () async {
      final high = await loadTestWaveform(frequency: 8000);
      final bus = newBus()..playOnEngine();
      final voice = bus.play(high);
      final unfiltered = level(await captureOutput());

      final biquad = bus.filters.biquadResonantFilter..activate();
      biquad.type().value = BiquadResonantEnum.type.min; // low-pass
      biquad.frequency().value = 200;
      final filtered = level(await captureOutput());
      expect(filtered, lessThan(unfiltered * 0.1));

      // The same tone played on the engine is not filtered.
      await soloud.stop(voice);
      final direct = soloud.play(high);
      expect(level(await captureOutput()) / unfiltered, closeTo(1, 0.1));
      await soloud.stop(direct);

      biquad.deactivate();
      bus.play(high);
      expect(level(await captureOutput()) / unfiltered, closeTo(1, 0.1));
    });
  });

  group('a disposed bus', () {
    late Bus bus;
    setUp(() {
      bus = soloud.createMixingBus()
        ..playOnEngine()
        ..dispose();
    });

    test('throws SoLoudBusDisposedDartException', () {
      final disposed = throwsA(isA<SoLoudBusDisposedDartException>());
      expect(bus.dispose, disposed);
      expect(bus.playOnEngine, disposed);
      expect(() => bus.play(tone), disposed);
      expect(() => bus.play3d(tone, 0, 0, 0), disposed);
      expect(() => bus.playClocked(tone, Duration.zero), disposed);
      expect(() => bus.playScheduled(tone, Duration.zero), disposed);
      expect(() => bus.annexSound(const SoundHandle(1)), disposed);
      expect(bus.setChannels, disposed);
      expect(bus.getVisualizationEnabled, disposed);
      expect(() => bus.getChannelVolume(0), disposed);
    });

    test('reports no voices', () {
      expect(bus.getActiveVoiceCount(), 0);
    });

    test('SoLoud.play on its id throws SoLoudBusIdNotFoundCppException', () {
      expect(
        () => soloud.play(tone, busId: bus.busId),
        throwsA(isA<SoLoudBusIdNotFoundCppException>()),
      );
    });

    test('its filters throw SoLoudBusIdNotFoundCppException', () {
      final notFound = throwsA(isA<SoLoudBusIdNotFoundCppException>());
      expect(bus.filters.echoFilter.activate, notFound);
      expect(bus.filters.echoFilter.deactivate, notFound);
      expect(() => bus.filters.echoFilter.wet().value, notFound);
      expect(() => bus.filters.echoFilter.wet().value = 0.5, notFound);
      expect(bus.filters.echoFilter.isActive, isFalse);
    });
  });

  test('getChannelVolume is 0 outside the bus channels', () {
    final bus = newBus()
      ..setChannels(channels: Channels.mono)
      ..playOnEngine();
    expect(bus.getChannelVolume(1), 0);
    expect(bus.getChannelVolume(-1), 0);
  });
}

# Mixing bus API reference

Verified against `lib/src/mixing_bus.dart` and `lib/src/soloud.dart`.

## Buses registry (singleton)

```dart
final all = Buses().buses;                    // List<Bus>, live buses only
final sfx = Buses().byName('SFX');            // throws StateError if missing
final sfx2 = Buses().byName('SFX', orElse: () => fallbackBus);
final b = Buses().byId(1);                    // same lookup semantics
```

Buses are added on construction and removed by `dispose()`. Bus equality is by
`busId`.

## Bus lifecycle

```dart
final bus = SoLoud.instance.createMixingBus(name: 'Music'); // busId >= 1
final handle = bus.playOnEngine(volume: 0.8, paused: false); // SoundHandle
bus.isActive;      // true once playOnEngine succeeded
bus.soundHandle;   // the bus's own voice handle (null before playOnEngine)
bus.dispose();     // stops ALL sounds routed through the bus
```

`playOnEngine` again on the same bus stops the earlier bus voice — a bus can
only play once at a time.

## Play variants (all synchronous, all return SoundHandle)

```dart
SoundHandle play(AudioSource sound, {
  double volume = 1, double pan = 0, bool paused = false,
  bool looping = false, Duration loopingStartAt = Duration.zero,
  Duration? loopingEndAt, int? loopingStartOffsetAt,
  int? loopingEndOffsetAt, double scale = 1,
});

SoundHandle playClocked(AudioSource sound, Duration soundTime, {
  double volume = 1, double pan = 0, double scale = 1,
  bool looping = false, ... // same looping params
});

SoundHandle playScheduled(AudioSource sound, Duration atTime, {
  Duration? duration, double volume = 1, double pan = 0, double scale = 1,
  bool looping = false, ... // absolute engine time, sample-accurate
});

SoundHandle play3d(AudioSource sound, double posX, double posY, double posZ, {
  double velX = 0, double velY = 0, double velZ = 0,
  double volume = 1, bool paused = false, bool looping = false, ...,
  double scale = 1,
});

SoundHandle play3dClocked(AudioSource sound, Duration soundTime,
    double posX, double posY, double posZ, {...});

SoundHandle play3dScheduled(AudioSource sound, Duration atTime,
    double posX, double posY, double posZ, {Duration? duration, ...});
```

Each is a convenience wrapper over the `SoLoud.instance` method of the same
name with `busId: busId`. Equivalent direct form:

```dart
SoLoud.instance.play(sound, busId: sfxBus.busId); // busId defaults to 0 = engine
```

`scale` is the relative playback speed multiplier (1.0 = normal).

## Routing a live voice

```dart
final handle = SoLoud.instance.play(someSound); // started on the engine
sfxBus.annexSound(handle); // now plays through sfxBus
```

## Channels and metering

```dart
bus.setChannels(channels: Channels.mono); // default Channels.stereo
final left = bus.getChannelVolume(0);  // needs setVisualizationEnabled(true)
```

`Channels`: `mono(1)`, `stereo(2)`, `quad(4)`, `surround51(6)`, `dolby71(8)`.

## Exceptions

- `SoLoudBusDisposedDartException` — any `Bus` method after `dispose()`
  (including a second `dispose()`).
- `SoLoudBackendNotInitedException` — `playOnEngine()` before engine init.
- `SoLoudBusIdNotFoundCppException` — bus unknown to the C++ side.
- `SoLoudFailedToStartPlaybackCppException` — engine couldn't create the bus
  voice.

## AudioManager pattern (music/SFX/voice + ducking)

```dart
class AudioManager {
  late final Bus musicBus;
  late final Bus sfxBus;
  late final Bus voiceBus;

  Future<void> init() async {
    await SoLoud.instance.init();
    musicBus = SoLoud.instance.createMixingBus(name: 'Music');
    sfxBus = SoLoud.instance.createMixingBus(name: 'SFX');
    voiceBus = SoLoud.instance.createMixingBus(name: 'Voice');
    musicBus.playOnEngine(volume: 0.7);
    sfxBus.playOnEngine();
    voiceBus.playOnEngine(volume: 0.9);
  }

  Future<void> playMusic(String asset) async {
    final music = await SoLoud.instance.loadAsset(asset);
    musicBus.play(music, looping: true);
  }

  Future<void> playSfx(String asset) async {
    sfxBus.play(await SoLoud.instance.loadAsset(asset));
  }

  void duckMusic() =>
      SoLoud.instance.setVolume(musicBus.soundHandle!, 0.3);
  void restoreMusic() =>
      SoLoud.instance.setVolume(musicBus.soundHandle!, 0.7);

  void dispose() {
    musicBus.dispose();
    sfxBus.dispose();
    voiceBus.dispose();
  }
}
```

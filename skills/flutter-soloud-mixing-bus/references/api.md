# Mixing bus API reference

Verified against `lib/src/mixing_bus.dart` and `lib/src/soloud.dart`.

## Buses registry (singleton)

```dart
final all = Buses().buses;                    // List<Bus>, live buses only
final sfx = Buses().byName('SFX');            // Bus?; throws StateError if missing
final sfx2 = Buses().byName('SFX', orElse: () => fallbackBus);
final b = Buses().byId(1);                    // same lookup semantics
```

Buses are added on construction and removed by `dispose()`; `deinit()` removes
and invalidates all of them. Bus equality is identity (one `Bus` object per
native bus); ids start again from 1 after a new `init()`.

## Bus lifecycle

```dart
final bus = SoLoud.instance.createMixingBus(name: 'Music'); // busId >= 1
final handle = bus.playOnEngine(volume: 0.8, paused: false); // SoundHandle
bus.isActive;      // true once playOnEngine succeeded
bus.soundHandle;   // the bus's own voice handle (null before playOnEngine)
bus.dispose();     // stops ALL sounds routed through the bus
```

`playOnEngine` again on the same bus stops the earlier bus voice — a bus can
only play once at a time. Until `playOnEngine` the play variants below throw
`SoLoudFailedToStartPlaybackCppException`.

The bus handle takes the ordinary voice calls:

```dart
final h = bus.soundHandle!;
SoLoud.instance.setVolume(h, 0.5);                                  // slider
SoLoud.instance.fadeVolume(h, 0.3, const Duration(milliseconds: 300)); // duck
SoLoud.instance.setPause(h, true);                                  // freeze the group
```

Nesting:

```dart
final game = SoLoud.instance.createMixingBus(name: 'Game')..playOnEngine();
final sfx = SoLoud.instance.createMixingBus(name: 'SFX');
game.annexSound(sfx.playOnEngine()); // sfx now plays through game
```

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

No-op when `sfxBus` isn't playing or the handle is no longer valid.

## Filters

```dart
final lowPass = musicBus.filters.biquadResonantFilter..activate();
// Parameters: methods without arguments; the bus must be playing.
lowPass.frequency().value = 400;
lowPass.wet().fadeFilterParameter(
  to: 0,
  time: const Duration(milliseconds: 300),
);
lowPass.deactivate();
```

At most 8 filters per bus. Details in **flutter-soloud-filters**.

## Channels and metering

```dart
bus.setChannels(channels: Channels.mono); // default Channels.stereo
bus.setVisualizationEnabled(true);     // this bus's own analyzer
final left = bus.getChannelVolume(0);  // peak of the latest analyzed window
bus.audioVisualizationEvents.listen((data) {
  // wave/FFT of what plays through the bus, before its filters/volume/pan
});
```

`Channels`: `mono(1)`, `stereo(2)`, `quad(4)`, `surround51(6)`, `dolby71(8)`.

## Exceptions

- `SoLoudNotInitializedException` — `createMixingBus()` before `init()`.
- `SoLoudBusDisposedDartException` — any `Bus` method after `dispose()`
  (including a second `dispose()`) or after `deinit()`.
- `SoLoudBusIdNotFoundCppException` — bus unknown to the C++ side: filters of
  a disposed bus, `SoLoud.instance.play(sound, busId: ...)` with its id.
- `SoLoudFailedToStartPlaybackCppException` — engine couldn't create the bus
  voice, or a sound was played onto a bus that isn't playing.
- `SoLoudSoundHandleNotFoundCppException` — a bus filter parameter used
  before `playOnEngine()`.
- `ArgumentError` — NaN or infinite `volume`/`pan`/`scale` in a play method.

## AudioManager pattern (music/SFX/voice + ducking)

```dart
class AudioManager {
  late Bus musicBus;
  late Bus sfxBus;
  late Bus voiceBus;
  final _sources = <String, AudioSource>{};

  /// Call again after every SoLoud.instance.init(): deinit() invalidates
  /// the buses and unloads the sources.
  Future<void> init() async {
    await SoLoud.instance.init();
    _sources.clear();
    musicBus = SoLoud.instance.createMixingBus(name: 'Music')
      ..playOnEngine(volume: 0.7);
    sfxBus = SoLoud.instance.createMixingBus(name: 'SFX')..playOnEngine();
    voiceBus = SoLoud.instance.createMixingBus(name: 'Voice')
      ..playOnEngine(volume: 0.9);
  }

  /// Load each asset once, play it many times.
  Future<AudioSource> _load(String asset) async =>
      _sources[asset] ??= await SoLoud.instance.loadAsset(asset);

  Future<SoundHandle> playMusic(String asset) async {
    final handle = musicBus.play(await _load(asset), looping: true);
    SoLoud.instance.setProtectVoice(handle, true);
    return handle;
  }

  Future<SoundHandle> playSfx(String asset) async =>
      sfxBus.play(await _load(asset));

  void setMusicVolume(double v) =>
      SoLoud.instance.setVolume(musicBus.soundHandle!, v);

  void duckMusic() => SoLoud.instance.fadeVolume(
    musicBus.soundHandle!,
    0.3,
    const Duration(milliseconds: 300),
  );
  void restoreMusic() => SoLoud.instance.fadeVolume(
    musicBus.soundHandle!,
    0.7,
    const Duration(milliseconds: 500),
  );

  void pauseGame({required bool paused}) =>
      SoLoud.instance.setPause(sfxBus.soundHandle!, paused);

  void dispose() {
    musicBus.dispose();
    sfxBus.dispose();
    voiceBus.dispose();
  }
}
```

For latency-sensitive effects preload the sources (`_load`) at startup so
`playSfx` does not wait for a decode on the first shot.

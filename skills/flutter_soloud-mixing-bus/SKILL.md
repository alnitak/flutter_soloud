---
name: flutter_soloud-mixing-bus
version: 1
description: Teaches how to use flutter_soloud mixing buses (Bus/Buses) to route groups of sounds through a shared sub-mix for collective volume control and shared filters. Use when the user asks for separate music/SFX/UI volume sliders, grouped effects, ducking music under voice, routing sounds between categories at runtime, or a per-category audio mixer in a game or app using flutter_soloud.
---

# Mixing buses (sub-mixes) in flutter_soloud

A `Bus` is a virtual mixer channel: sounds are routed *through* it, so one
volume change or one filter affects everything on the bus. The canonical use
is separate `Music` / `SFX` / `UI` buses behind user volume sliders. Unlike
audioplayers/just_audio (one player per sound, no sub-mix concept), routing is
explicit: you create a bus, play **the bus itself** on the engine, then play
sounds onto it.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  // 1. Create the bus (synchronous, cheap).
  final sfxBus = SoLoud.instance.createMixingBus(name: 'SFX');

  // 2. REQUIRED: play the bus on the engine. Until this call, the bus
  //    (and everything routed through it) is silent.
  sfxBus.playOnEngine();

  // 3. Route sounds through the bus.
  final explosion = await SoLoud.instance.loadAsset('assets/explosion.mp3');
  sfxBus.play(explosion); // synchronous, returns SoundHandle

  // 4. Collective volume: set it on the bus's own handle, not per-sound.
  SoLoud.instance.setVolume(sfxBus.soundHandle!, 0.5);

  // 5. Shared filter on everything routed through the bus.
  sfxBus.filters.echoFilter.activate();

  // Cleanup: stops all sounds playing through the bus.
  sfxBus.dispose();
}
```

## The API shape

- `SoLoud.instance.createMixingBus({String name = ''})` → `Bus`. Synchronous;
  registers the bus in the `Buses()` registry. First bus gets `busId == 1`;
  `busId == 0` always means "the main engine output".
- `Buses()` singleton registry: `Buses().buses` (list of all live buses),
  `Buses().byName('SFX')`, `Buses().byId(1)`. Both lookups throw if not found
  unless you pass `orElse`.
- `bus.playOnEngine({double volume = 1.0, bool paused = false})` →
  `SoundHandle`. Makes the bus audible; stores the handle in
  `bus.soundHandle`. Calling it again on the same bus stops the earlier
  instance (only one bus voice at a time).
- Playing onto a bus — two equivalent ways:
  - `bus.play(sound, {volume, pan, paused, looping, ...})` and its siblings
    `bus.playClocked`, `bus.playScheduled`, `bus.play3d`, `bus.play3dClocked`,
    `bus.play3dScheduled` — thin wrappers that forward to the `SoLoud` method
    of the same name with `busId` set.
  - `SoLoud.instance.play(sound, busId: sfxBus.busId)` — every `SoLoud`
    play method takes an `int busId = 0` parameter.
- `bus.annexSound(handle)` — re-parent a *live* voice (by its `SoundHandle`)
  into this bus, e.g. move a sound already playing on the engine onto a
  filtered bus mid-playback.
- Bus volume: there is **no** `bus.volume` property. Use
  `SoLoud.instance.setVolume(bus.soundHandle!, v)` /
  `SoLoud.instance.getVolume(bus.soundHandle!)`. This scales every voice
  routed through the bus on top of each voice's own volume.
- `bus.filters` — a `FiltersSingle` scoped to this bus; filters activated
  here apply to all routed sounds only (engine-wide filters live on
  `SoLoud.instance.filters`). See skill `flutter_soloud-filters`.
- `bus.setChannels({Channels channels = Channels.stereo})` — bus output
  channel count (`Channels.mono/stereo/quad/surround51/dolby71`).
- `bus.getChannelVolume(channel)` — approximate per-channel output level for
  VU meters. Requires `SoLoud.instance.setVisualizationEnabled(true)` first;
  returns 0 for out-of-range channels.
- `bus.getActiveVoiceCount()` — voices currently playing through the bus;
  returns 0 if the bus isn't playing on the engine.
- `bus.isActive` — true once `playOnEngine()` succeeded.
- `bus.dispose()` — destroys the bus and **stops all sounds routed through
  it**; removes it from `Buses()`.

Everything above (except `createMixingBus` lookups and `dispose`) is
synchronous and returns `SoundHandle` directly — do not `await` them.

## Traps

- **Silent bus**: forgetting `playOnEngine()` is the #1 failure. Sounds
  "play" (handles are created, voices count up) but nothing is heard.
  `bus.soundHandle` stays `null` until then, so guard with
  `if (bus.soundHandle != null)` before volume/filter-parameter calls.
- **Do not invent `bus.volume` / `bus.setVolume()`**: they don't exist.
  Volume goes through `SoLoud.instance.setVolume(bus.soundHandle!, v)`.
  Models coming from audioplayers habitually look for a per-player volume
  setter — route them to the bus handle instead.
- **No `await` on bus play methods**: `bus.play(...)` returns `SoundHandle`,
  not a `Future`. `SoLoud.instance.loadAsset` is the async part; playback is
  synchronous.
- **Disposed bus throws**: every `Bus` method throws
  `SoLoudBusDisposedDartException` after `dispose()`. Double-dispose also
  throws — null out your reference after disposing.
- **Create buses after `await SoLoud.instance.init()`**: the constructor calls
  into the native engine, and `playOnEngine()` throws
  `SoLoudBackendNotInitedException` when the engine isn't up.
- **`byName`/`byId` throw when missing**: they use `firstWhere`; pass
  `orElse` or check `Buses().buses` first if the bus may not exist.
- **Voice limit**: bus voices count against the engine's active-voice cap
  (default 16, raise with `SoLoud.instance.setMaxActiveVoiceCount`). Hitting
  it is not an error — the new sound just doesn't play.
- **`annexSound` needs a live handle**: annexing a finished/invalid handle is
  a no-op on the native side; check `sound.handles` / `isActive` first.
- **Filters need the bus handle for per-instance parameters**: e.g.
  `bus.filters.pitchShiftFilter.shift(soundHandle: bus.soundHandle).value = 2.5;`
  after `activate()`. Plain `.shift.value` targets the global filter, not the
  bus voice.

## More depth

- `references/api.md` — full signatures of all six bus play variants, the
  `Buses` registry, exceptions, and the AudioManager (music/SFX/voice +
  ducking) pattern.
- Runnable demo: `example/lib/mixing_bus/mixing_bus.dart` in this repo —
  creates buses, plays/annexes sounds, per-bus volume slider, pitch-shift
  filter toggle, live voice counts.
- Upstream concepts: https://solhsa.com/soloud/mixbus.html

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

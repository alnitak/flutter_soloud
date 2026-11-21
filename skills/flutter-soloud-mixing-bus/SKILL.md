---
name: flutter-soloud-mixing-bus
version: 4
description: Teaches how to use flutter_soloud mixing buses (Bus/Buses) to route groups of sounds through a shared sub-mix for collective volume, pause, fades and shared filters. Use when the user asks for separate music/SFX/UI/voice volume sliders, a per-category audio mixer or settings screen, ducking music under dialogue, pausing or muting all gameplay sounds for a pause menu, one effect (reverb, low-pass) on a whole category, filters that must work on web, a per-group level meter, or routing playing sounds between categories at runtime in a game or app using flutter_soloud, and when code loops over handles or multiplies per-sound volumes to do any of this.
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

  // 2. REQUIRED: play the bus on the engine. Until this call the bus has
  //    no voice: playing a sound onto it throws.
  sfxBus.playOnEngine();

  // 3. Route sounds through the bus.
  final explosion = await SoLoud.instance.loadAsset('assets/explosion.mp3');
  sfxBus.play(explosion); // synchronous, returns SoundHandle

  // 4. Collective volume: set it on the bus's own handle, not per-sound.
  SoLoud.instance.setVolume(sfxBus.soundHandle!, 0.5);

  // 5. Shared filter on everything routed through the bus. Parameters are
  //    methods called without arguments.
  sfxBus.filters.echoFilter.activate();
  sfxBus.filters.echoFilter.delay().value = 0.2;

  // Cleanup: stops all sounds playing through the bus.
  sfxBus.dispose();
}
```

## The API shape

- `SoLoud.instance.createMixingBus({String name = ''})` → `Bus`. Synchronous;
  registers the bus in the `Buses()` registry. Throws
  `SoLoudNotInitializedException` before `init()`. First bus gets
  `busId == 1`; `busId == 0` always means "the main engine output".
- `Buses()` singleton registry: `Buses().buses` (list of all live buses),
  `Buses().byName('SFX')`, `Buses().byId(1)` (both return `Bus?`). Both
  lookups throw `StateError` if not found unless you pass `orElse`.
- `bus.playOnEngine({double volume = 1.0, bool paused = false})` →
  `SoundHandle`. Makes the bus audible; stores the handle in
  `bus.soundHandle`. Calling it again on the same bus stops the earlier
  instance (only one bus voice at a time). `bus.soundHandle` is an ordinary
  voice handle: `setVolume`, `fadeVolume`, `setPause`, `setPan` on it act on
  the whole bus (a paused bus stops mixing its voices; they resume where they
  were).
- Playing onto a bus — two equivalent ways:
  - `bus.play(sound, {volume, pan, paused, looping, ...})` and its siblings
    `bus.playClocked`, `bus.playScheduled`, `bus.play3d`, `bus.play3dClocked`,
    `bus.play3dScheduled` — thin wrappers that forward to the `SoLoud` method
    of the same name with `busId` set.
  - `SoLoud.instance.play(sound, busId: sfxBus.busId)` — every `SoLoud`
    play method takes an `int busId = 0` parameter.
- `bus.annexSound(handle)` — re-parent a *live* voice (by its `SoundHandle`)
  into this bus, e.g. move a sound already playing on the engine onto a
  filtered bus mid-playback. Does nothing while the bus isn't playing.
- Nested buses: a bus is a voice too, so
  `outer.annexSound(inner.playOnEngine())` plays `inner` through `outer`.
  Volume, pause and filters of `outer` then apply to everything in `inner`;
  disposing `outer` stops `inner`'s voice.
- Bus volume: there is **no** `bus.volume` property. Use
  `SoLoud.instance.setVolume(bus.soundHandle!, v)` /
  `SoLoud.instance.getVolume(bus.soundHandle!)`. This scales every voice
  routed through the bus on top of each voice's own volume.
- `bus.filters` — a `FiltersSingle` scoped to this bus; filters activated
  here apply to all routed sounds only (engine-wide filters live on
  `SoLoud.instance.filters`). Parameters are methods called without
  arguments: `bus.filters.biquadResonantFilter.frequency().value = 400`,
  `bus.filters.echoFilter.wet().fadeFilterParameter(to: 0, time: ...)`.
  Bus filters work on web (per-sound filters do not). See skill
  **flutter-soloud-filters**.
- `bus.setChannels({Channels channels = Channels.stereo})` — bus output
  channel count (`Channels.mono/stereo/quad/surround51/dolby71`).
- `bus.setVisualizationEnabled(true)` / `bus.getVisualizationEnabled()` /
  `bus.audioVisualizationEvents` — wave/FFT of what plays through this bus,
  with the same options and packets as `SoLoud.instance.setVisualizationEnabled`.
  The data is taken *before* the bus's own filters, volume and pan; nothing is
  emitted while nothing is mixed through the bus (not playing, paused, outer
  bus paused), and a playing empty bus emits silence. See skill
  `flutter-soloud-visualization`.
- `bus.getChannelVolume(channel)` — approximate per-channel level (the peak of
  the latest analyzed window) for VU meters. Requires
  `bus.setVisualizationEnabled(true)` first (the engine-wide
  `SoLoud.instance.setVisualizationEnabled` does not feed it); returns 0 for
  out-of-range channels. Taken before the bus's filters, volume and pan.
- `bus.getActiveVoiceCount()` — voices currently playing through the bus;
  returns 0 if the bus isn't playing on the engine.
- `bus.isActive` — true once `playOnEngine()` succeeded (false again after
  `dispose()` or `deinit()`).
- `bus.dispose()` — destroys the bus and **stops all sounds routed through
  it**; removes it from `Buses()` and closes `audioVisualizationEvents`.

Everything above is synchronous and the play methods return `SoundHandle`
directly — do not `await` them.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| Music / SFX / UI / voice volume sliders done by storing a category volume and multiplying it into every `play(volume:)`, or by looping over handles with `setVolume` | One `Bus` per category (`createMixingBus` + `playOnEngine`), sounds played with `bus.play(...)`, slider → `SoLoud.instance.setVolume(bus.soundHandle!, v)` | One call changes every playing and future voice of the category, on top of each voice's own volume; no handle bookkeeping. Each bus uses one voice of the active-voice limit. |
| Ducking music under dialogue or a cutscene with an abrupt `setVolume`, or a `Timer` stepping the volume | `SoLoud.instance.fadeVolume(musicBus.soundHandle!, 0.3, const Duration(milliseconds: 300))` when the line starts, the reverse when it ends (**flutter-soloud-volume-pan**) | The ramp runs in the engine: no audible jump or steps, and it covers every track on the music bus, also one that starts during the duck. |
| A pause menu that loops over handles calling `setPause`, or `stopAll()` and restarting sounds on resume | Gameplay sounds on one bus (or several nested under one), then `SoLoud.instance.setPause(gameBus.soundHandle!, true)`; UI sounds stay on another bus or on the engine | One call freezes the whole group and it resumes exactly where it was; menu clicks keep playing. |
| The same filter activated on many `AudioSource`s (cave reverb on every SFX, low-pass on all music in a menu), or `sound.filters` in an app that targets web | The filter on the category's bus: `bus.filters.freeverbFilter` / `echoFilter` / `biquadFilter`, parameters faded with `fadeFilterParameter` (**flutter-soloud-filters**) | One filter instance instead of one per voice (less CPU), switched for the whole category at once; per-sound filters throw on web while bus filters work there. |
| A voice stopped and played again only to put it under an effect or another volume group (walking indoors, a radio picked up) | `targetBus.annexSound(handle)` | Moves the live voice without a restart or a gap; it keeps its position, volume and handle. |
| A per-category level meter or visualizer computed from decoded samples, or the global visualization used where only the music should react | `bus.setVisualizationEnabled(true)` + `bus.getChannelVolume(ch)` / `bus.audioVisualizationEvents` (**flutter-soloud-visualization**) | The engine analyzes just what plays through that bus. The data is taken before the bus's own volume and filters, so a muted bus still shows activity. |

## Traps

- **Bus not played**: forgetting `playOnEngine()` is the #1 failure.
  `bus.play(...)` and `SoLoud.instance.play(sound, busId: ...)` then throw
  `SoLoudFailedToStartPlaybackCppException`, `annexSound` does nothing, and
  `bus.soundHandle` is still `null`. Call `playOnEngine()` right after
  `createMixingBus()`; guard later calls with `bus.isActive`.
- **Do not invent `bus.volume` / `bus.setVolume()`**: they don't exist.
  Volume goes through `SoLoud.instance.setVolume(bus.soundHandle!, v)`.
  Models coming from audioplayers habitually look for a per-player volume
  setter — route them to the bus handle instead.
- **No `await` on bus play methods**: `bus.play(...)` returns `SoundHandle`,
  not a `Future`. `SoLoud.instance.loadAsset` is the async part; playback is
  synchronous.
- **Disposed bus throws**: every `Bus` method throws
  `SoLoudBusDisposedDartException` after `dispose()` (except
  `getActiveVoiceCount()`, which returns 0). Double-dispose also throws — null
  out your reference after disposing. Its `filters` and
  `SoLoud.instance.play(sound, busId: bus.busId)` throw
  `SoLoudBusIdNotFoundCppException`.
- **`deinit()` invalidates every bus** (also the `deinit()` inside a second
  `init()`): `Buses().buses` is emptied and the old `Bus` objects behave as
  disposed. Create the buses again after each `init()`; don't keep them in
  `static final` fields that outlive the engine. Bus ids restart from 1 and
  `Bus` equality is identity, so compare `Bus` objects, not stored ids.
- **Create buses after `await SoLoud.instance.init()`**: `createMixingBus()`
  throws `SoLoudNotInitializedException` before it.
- **`byName`/`byId` throw `StateError` when missing**: they use `firstWhere`;
  pass `orElse` or check `Buses().buses` first if the bus may not exist. Names
  are not unique: `byName` returns the first match.
- **Voice limit**: each playing bus and each voice on it count against the
  engine's active-voice cap (default 16, raise with
  `SoLoud.instance.setMaxActiveVoiceCount`). Beyond it some voices are not
  heard and no exception is thrown; protect long-running ones with
  `SoLoud.instance.setProtectVoice(handle, true)` (see
  **flutter-soloud-playback**).
- **`annexSound` needs a live handle and a playing bus**: annexing a
  finished/invalid handle, or into a bus that isn't playing, is a no-op; the
  voice keeps playing where it was.
- **Bus filter parameters need the bus to be playing and no handle**:
  `bus.filters.pitchShiftFilter.shift().value = 2.5;` after `activate()` and
  `playOnEngine()`. Before `playOnEngine()` the parameter calls throw
  `SoLoudSoundHandleNotFoundCppException` (`activate()` itself works at any
  time). Don't pass `soundHandle:` — on web that throws
  `SoLoudFilterForSingleSoundOnWebDartException`.
- **NaN or infinite `volume`/`pan`/`scale`** in any play method throw
  `ArgumentError`, like on `SoLoud.instance.play`.

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

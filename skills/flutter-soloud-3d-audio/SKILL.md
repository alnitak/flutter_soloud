---
name: flutter-soloud-3d-audio
version: 4
description: Teaches positional (3D) audio in flutter_soloud — play3d/play3dClocked/play3dScheduled, listener position/orientation/velocity, per-voice and per-sound attenuation and Doppler (set3dSource*, set3dDefault*), the per-frame update pattern, and batching the updates of many moving sources (set3dAutoUpdate/update3dAudio). Use when the user asks for spatial audio, positional sound effects, distance-based volume falloff, sounds attached to game entities (enemies, vehicles, gunfire or footsteps at a world position), Doppler effects, or left/right panning based on world position, or when code computes pan and volume by hand from positions.
---

flutter_soloud wraps the SoLoud C++ engine's built-in 3D audio. Every voice started with `play3d` has a 3D position and velocity; a single global "listener" (usually your camera or player) hears them. The coordinate system is right-handed (Y up, X right, Z toward the viewer). Unlike audioplayers/just_audio there is no asset "player" object per sound: you get a `SoundHandle` per playing instance and mutate its 3D state through `SoLoud.instance` setters. Also unlike the web Audio API, SoLoud does **not** move anything for you — positions and velocities are snapshots you must update yourself each tick.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  final sound = await SoLoud.instance.loadAsset('assets/sfx/whoosh.mp3');

  // Listener at origin, looking down -Z, head up along +Y.
  SoLoud.instance.set3dListenerParameters(
    0, 0, 0, // position
    0, 0, -1, // at (looking direction)
    0, 1, 0, // up
    0, 0, 0, // velocity (needed for Doppler)
  );

  // Play at (10, 0, -5) moving toward the listener at 5 units/s.
  final handle = SoLoud.instance.play3d(
    sound,
    10, 0, -5, // posX, posY, posZ (positional!)
    velX: -5, // velocity, for Doppler
    looping: true,
  );

  // Required to hear distance falloff — default attenuation is NONE.
  SoLoud.instance.set3dSourceAttenuation(handle, 2, 1); // linear
  SoLoud.instance.set3dSourceMinMaxDistance(handle, 1, 50);

  // Or once on the sound, before playing: every voice started from it
  // later begins with these values.
  SoLoud.instance.set3dDefaultAttenuation(sound, 2, 1);
  SoLoud.instance.set3dDefaultMinMaxDistance(sound, 1, 50);
}
```

## The API shape

All methods are on `SoLoud.instance` (singleton), all positions/velocities are `double` in your world units. Playback methods return `SoundHandle` **synchronously** (not a `Future`) — different from the `Future<...>`-returning patterns in other packages.

Every 3D method (playback, listener and source setters, sound speed) throws `SoLoudNotInitializedException` before `init()` completes, and `ArgumentError` if a position, velocity, distance, rolloff, Doppler factor, `volume` or `scale` is NaN or infinite.

Playback:

- `SoundHandle play3d(AudioSource sound, double posX, double posY, double posZ, {double velX = 0, double velY = 0, double velZ = 0, int busId = 0, double volume = 1, bool paused = false, bool looping = false, Duration loopingStartAt = Duration.zero, Duration? loopingEndAt, int? loopingStartOffsetAt, int? loopingEndOffsetAt, double scale = 1})` — one-shot, lowest latency; start is quantized to output-buffer boundaries. There is no `pan`: the position decides it. With `paused: true` you can set attenuation and distances first, then `setPause(handle, false)`, so the first buffer already has the falloff.
- `SoundHandle play3dClocked(AudioSource sound, Duration soundTime, double posX, double posY, double posZ, {...})` — pass your app's monotonically increasing "physics time"; the engine spaces rapid-fire sounds sample-accurately so they don't clump at the next buffer boundary. Costs ~2 buffers of latency. No `paused` parameter. Use for machine-gun / footsteps / rhythmic 3D SFX; pattern in **flutter-soloud-scheduling**.
- `SoundHandle play3dScheduled(AudioSource sound, Duration atTime, double posX, double posY, double posZ, {Duration? duration, ...})` — starts at an absolute engine time (`SoLoud.instance.getEngineTime()`), sample-accurate; `duration` auto-stops it at `atTime + duration`. No `paused` parameter.

Listener (one global listener; default position is `(0,0,0)`):

- `set3dListenerParameters(posX, posY, posZ, atX, atY, atZ, upX, upY, upZ, velocityX, velocityY, velocityZ)` — 12 **positional** doubles, no named parameters.
- `set3dListenerPosition(x, y, z)` / `set3dListenerAt(x, y, z)` / `set3dListenerUp(x, y, z)` / `set3dListenerVelocity(x, y, z)` — individual setters. `at` is the look direction; `up` is the head-up vector.

Per-source (take the `SoundHandle` returned by `play3d*`):

- `set3dSourceParameters(handle, posX, posY, posZ, velocityX, velocityY, velocityZ)` — positional doubles.
- `set3dSourcePosition(handle, x, y, z)` / `set3dSourceVelocity(handle, x, y, z)`.
- `set3dSourceMinMaxDistance(handle, minDistance, maxDistance)` — defaults are `1` and `1000000`. Inside `min` the sound is at full volume; beyond `max` it no longer gets quieter.
- `set3dSourceAttenuation(handle, int attenuationModel, double attenuationRolloffFactor)` — models: `0` NO_ATTENUATION, `1` INVERSE_DISTANCE, `2` LINEAR_DISTANCE, `3` EXPONENTIAL_DISTANCE. **Default is `0` (none)** with rolloff `1`.
- `set3dSourceDopplerFactor(handle, double dopplerFactor)` — `0` disables, `1` normal, `>1` exaggerated.

Per-sound defaults (take the `AudioSource`; a voice copies them when it is created, so they hold from its first sample):

- `set3dDefaultMinMaxDistance(sound, minDistance, maxDistance)`, `set3dDefaultAttenuation(sound, attenuationModel, attenuationRolloffFactor)`, `set3dDefaultDopplerFactor(sound, dopplerFactor)` — same values and defaults as the handle setters above. They apply to voices started **after** the call; voices already playing keep theirs, and a handle setter still changes one voice. A sound that is not loaded is ignored.

Global Doppler tuning:

- `set3dSoundSpeed(double speed)` / `double get3dSoundSpeed()` — default `343` (meters/second in dry air at ~20 °C). SoLoud doesn't know your unit scale, so if 1 world unit ≠ 1 meter, scale the sound speed accordingly or Doppler will be wrong.

Batched updates (many moving sources):

- `set3dAutoUpdate(bool enabled)` / `bool get3dAutoUpdate()` — default `true`: every 3D setter (listener, source, sound speed) recomputes all the 3D voices before it returns. With `false` the setters only store their values. `init()` restores `true`; passing `true` applies what was stored.
- `update3dAudio()` — applies the stored values to all the 3D voices in one pass. Needed only after `set3dAutoUpdate(false)`.

## Per-frame update pattern

Nothing moves unless you move it. In your game loop / `Ticker` / `onTick`:

```dart
void update(double dt) {
  // Listener follows the camera/player every frame.
  SoLoud.instance.set3dListenerParameters(
    cam.x, cam.y, cam.z,
    cam.forwardX, cam.forwardY, cam.forwardZ,
    cam.upX, cam.upY, cam.upZ,
    cam.velX, cam.velY, cam.velZ, // for Doppler
  );

  // Each moving emitter updates position AND velocity.
  for (final e in movingEmitters) {
    SoLoud.instance.set3dSourceParameters(
      e.handle, e.x, e.y, e.z, e.vx, e.vy, e.vz,
    );
  }
}
```

With many moving emitters, defer the recomputation and do it once per frame:

```dart
SoLoud.instance.set3dAutoUpdate(false); // once, after init()

void update(double dt) {
  SoLoud.instance.set3dListenerParameters(/* ... */);
  for (final e in movingEmitters) {
    SoLoud.instance.set3dSourceParameters(e.handle, /* ... */);
  }
  SoLoud.instance.update3dAudio(); // one pass for the listener and all sources
}
```

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| Pan and volume computed by hand from positions every frame (`setPan((x - playerX) / width)`, `setVolume(1 / distance)`) | `play3d(sound, x, y, z)` + `set3dSourceAttenuation` / `set3dSourceMinMaxDistance`, and `set3dListenerParameters` once per frame | The engine computes panning, distance falloff and Doppler from positions; the game code only reports where things are. |
| `play3d` in use but "everything has the same loudness" or "3D does not work" | `set3dDefaultAttenuation(sound, 2, 1)` (linear) and `set3dDefaultMinMaxDistance(sound, min, max)` once after loading; or the `set3dSource*` setters on each handle | The default model is no attenuation and the default max distance is 1000000, so there is panning but no audible falloff. |
| The same `set3dSourceAttenuation` / `set3dSourceMinMaxDistance` / `set3dSourceDopplerFactor` calls repeated after every `play3d` of a sound, or `play3d(paused: true)` + setters + unpause only to configure the voice | `set3dDefaultAttenuation`, `set3dDefaultMinMaxDistance`, `set3dDefaultDopplerFactor` once on the `AudioSource` | Every later voice starts with the values from its first sample: nothing to repeat, no moment at full volume, and it works with `play3dClocked` / `play3dScheduled` (no `paused`) and with `set3dAutoUpdate(false)`. For values that differ per voice of the same sound, set the defaults right before each `play3d` or use the handle setters. |
| Repeated positional SFX (an enemy machine gun, NPC footsteps) played with `play3d` from a `Timer` or the game loop | `play3dClocked(sound, idealTime, x, y, z)` with an accumulated ideal time, or `play3dScheduled` for planned times. **flutter-soloud-scheduling** | `play3d` starts at the next output buffer, so close shots clump and the cadence is uneven; the clocked call is sample-accurate. Cost: a constant lead of about two buffers. |
| A 2D game (side-scroller, top-down) with pixel coordinates that wants positional sound | The same 3D calls with the unused axis at 0, the listener at the camera or player, and min/max distances in pixels | No separate 2D panning code. Distances, positions and `set3dSoundSpeed` share one unit: with pixels, raise the sound speed or leave velocities at 0. |
| A pass-by effect (cars, projectiles, a siren) faked by changing `setRelativePlaySpeed` by hand | Pass `velX/velY/velZ` (or `set3dSourceVelocity`) and the listener velocity; tune with `set3dSourceDopplerFactor` | The Doppler shift follows the real relative speed: higher when approaching, lower when receding. |
| Many emitters in the world (torches, NPCs, distant shots) using up the voices | Linear attenuation with a real max distance, `setInaudibleBehavior(handle, false, true)` on one-shots, `setMaxActiveVoiceCount`. **flutter-soloud-playback** | A one-shot that becomes inaudible is stopped and frees its voice; the default pauses it and resumes it when audible, which is right for looping ambience. A voice is inaudible when the falloff brings its volume close to zero: linear attenuation with rolloff 1 does that at the max distance. |
| Many emitters (tens or more) moved every frame, or 3D updates showing up in a profile | `set3dAutoUpdate(false)` once after `init()`, the setters as before, then one `update3dAudio()` at the end of the frame | By default each setter recomputes every 3D voice, so N moving sources cost N full passes per frame; deferred it is one pass. Nothing is heard of a setter until `update3dAudio()`, also the attenuation set on a new handle. |
| The listener or an emitter updated with several calls per frame (`set3dListenerPosition` + `set3dListenerAt` + `set3dListenerVelocity`, or position + velocity) | One `set3dListenerParameters(...)` and one `set3dSourceParameters(handle, ...)` per emitter per frame | Each setter recomputes the 3D state on the native side; one call per object keeps position and velocity consistent and saves native calls. |

## Traps

- **Default attenuation is `0` (NO_ATTENUATION).** If you `play3d` and never call `set3dSourceAttenuation` (or `set3dDefaultAttenuation` on the sound), you get left/right panning from position but **no volume falloff with distance** — the #1 "3D doesn't work" report. Pick a model (linear `2` is a good default for games) and set sensible min/max distances (e.g. `1`–`50` for meter-scale worlds; leaving max at the `1000000` default makes falloff imperceptible).
- **`set3dDefault*` does not touch playing voices.** It changes what later voices of that sound start with. To change a voice that is already playing use the `set3dSource*` setter with its handle.
- **Positions are snapshots.** A sound stays where `play3d` put it until you call `set3dSourcePosition`/`set3dSourceParameters`. No automatic binding to a game object; update every tick while the voice is alive.
- **Velocity doesn't move the source; it only feeds Doppler.** You must supply both the position delta (via position updates) and the velocity vector yourself. Same for the listener. Zero velocity = no Doppler.
- **`at` and `up` must not be parallel** (and should be roughly orthogonal and normalized). If both are e.g. `(0,1,0)` the listener orientation is degenerate and panning/orientation glitches or collapses.
- **With `set3dAutoUpdate(false)` nothing changes until `update3dAudio()`.** A missing call means sources and listener seem frozen. `play3d` still places a new voice by itself, but the attenuation, distances and Doppler factor set on its handle wait for the next `update3dAudio()` (the `set3dDefault*` values of its sound do not: the voice starts with them). `init()` goes back to the automatic update: set it again after every `init()`.
- **`set3dListenerParameters` takes 12 positional doubles** — no named arguments, and the source setters are positional too. Don't pass named args copied from the web docs; it won't compile.
- **`play3d` returns `SoundHandle` synchronously.** No `await`. The async part is `init()` and `loadAsset`/`loadFile`.
- **NaN or infinity throws `ArgumentError`.** Normalizing a zero-length direction (`v / v.length`) gives NaN: guard it before calling the listener or source setters.
- **Handle lifetime:** once a non-looping sound finishes, its handle is invalid; per-source setters on a dead handle silently do nothing. Track `sound.soundEvents` / `allInstancesFinished` if you reuse handles.
- **Voice limit:** default max active voices is 16 (`setMaxActiveVoiceCount` to change). At the limit, the oldest unprotected instance of the same sound is stolen; if that sound has no voice to give up, the new play silently produces a handle addressing no voice. See `setProtectVoice` and `setInaudibleBehavior` in **flutter-soloud-playback**.
- **`play3d` start is buffer-quantized.** Rapidly triggered SFX (gunfire) clump to buffer boundaries — use `play3dClocked` (or `play3dScheduled` with `getEngineTime()`) for sample-accurate spacing.
- **Doppler scale mismatch:** `set3dSoundSpeed` default 343 assumes meters. If your world is in pixels or generic units, audible Doppler warble at game speeds means your velocities are huge relative to 343 — raise the sound speed or scale down.
- **Unit consistency:** attenuation distances, listener/source positions, and sound speed all share the same implicit unit. Mixing meters for distances and pixels for positions gives nonsense falloff.

## More depth

- Working test exercising the full API (listener, source, attenuation formulas, Doppler, sound speed): `test/common/three_d_audio_test.dart`. The example app (`example/lib/`) has no dedicated 3D demo screen.
- Doc page: https://docs.page/alnitak/flutter_soloud_docs/advanced/3d_audio.
- Upstream SoLoud 3D concepts (attenuation models, Doppler math): https://solhsa.com/soloud/concepts3d.html

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

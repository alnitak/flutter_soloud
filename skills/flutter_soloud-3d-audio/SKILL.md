---
name: flutter_soloud-3d-audio
version: 1
description: Teaches positional (3D) audio in flutter_soloud — play3d/play3dClocked/play3dScheduled, listener position/orientation/velocity, per-source attenuation and Doppler, and the per-frame update pattern. Use when the user asks for spatial audio, positional sound effects, distance-based volume falloff, sounds attached to game entities, Doppler effects, or left/right panning based on world position.
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
}
```

## The API shape

All methods are on `SoLoud.instance` (singleton), all positions/velocities are `double` in your world units. Playback methods return `SoundHandle` **synchronously** (not a `Future`) — different from the `Future<...>`-returning patterns in other packages.

Playback (all throw `SoLoudNotInitializedException` if you skipped `init`):

- `SoundHandle play3d(AudioSource sound, double posX, double posY, double posZ, {double velX = 0, double velY = 0, double velZ = 0, int busId = 0, double volume = 1, bool paused = false, bool looping = false, Duration loopingStartAt = Duration.zero, Duration? loopingEndAt, int? loopingStartOffsetAt, int? loopingEndOffsetAt, double scale = 1})` — one-shot, lowest latency; start is quantized to output-buffer boundaries.
- `SoundHandle play3dClocked(AudioSource sound, Duration soundTime, double posX, double posY, double posZ, {...})` — pass your app's monotonically increasing "physics time"; the engine spaces rapid-fire sounds sample-accurately so they don't clump at the next buffer boundary. Costs ~2 buffers of latency. Use for machine-gun / rhythmic 3D SFX.
- `SoundHandle play3dScheduled(AudioSource sound, Duration atTime, double posX, double posY, double posZ, {Duration? duration, ...})` — starts at an absolute engine time (`SoLoud.instance.getEngineTime()`), sample-accurate; `duration` auto-stops it at `atTime + duration`.

Listener (one global listener; default position is `(0,0,0)`):

- `set3dListenerParameters(posX, posY, posZ, atX, atY, atZ, upX, upY, upZ, velocityX, velocityY, velocityZ)` — 12 **positional** doubles, no named parameters.
- `set3dListenerPosition(x, y, z)` / `set3dListenerAt(x, y, z)` / `set3dListenerUp(x, y, z)` / `set3dListenerVelocity(x, y, z)` — individual setters. `at` is the look direction; `up` is the head-up vector.

Per-source (take the `SoundHandle` returned by `play3d*`):

- `set3dSourceParameters(handle, posX, posY, posZ, velocityX, velocityY, velocityZ)` — positional doubles.
- `set3dSourcePosition(handle, x, y, z)` / `set3dSourceVelocity(handle, x, y, z)`.
- `set3dSourceMinMaxDistance(handle, minDistance, maxDistance)` — defaults are `1` and `1000000`. Inside `min` the sound is at full volume; beyond `max` it no longer gets quieter.
- `set3dSourceAttenuation(handle, int attenuationModel, double attenuationRolloffFactor)` — models: `0` NO_ATTENUATION, `1` INVERSE_DISTANCE, `2` LINEAR_DISTANCE, `3` EXPONENTIAL_DISTANCE. **Default is `0` (none)** with rolloff `1`.
- `set3dSourceDopplerFactor(handle, double dopplerFactor)` — `0` disables, `1` normal, `>1` exaggerated.

Global Doppler tuning:

- `set3dSoundSpeed(double speed)` / `double get3dSoundSpeed()` — default `343` (meters/second in dry air at ~20 °C). SoLoud doesn't know your unit scale, so if 1 world unit ≠ 1 meter, scale the sound speed accordingly or Doppler will be wrong.

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

## Traps

- **Default attenuation is `0` (NO_ATTENUATION).** If you `play3d` and never call `set3dSourceAttenuation`, you get left/right panning from position but **no volume falloff with distance** — the #1 "3D doesn't work" report. Pick a model (linear `2` is a good default for games) and set sensible min/max distances (e.g. `1`–`50` for meter-scale worlds; leaving max at the `1000000` default makes falloff imperceptible).
- **Positions are snapshots.** A sound stays where `play3d` put it until you call `set3dSourcePosition`/`set3dSourceParameters`. No automatic binding to a game object; update every tick while the voice is alive.
- **Velocity doesn't move the source; it only feeds Doppler.** You must supply both the position delta (via position updates) and the velocity vector yourself. Same for the listener. Zero velocity = no Doppler.
- **`at` and `up` must not be parallel** (and should be roughly orthogonal and normalized). If both are e.g. `(0,1,0)` the listener orientation is degenerate and panning/orientation glitches or collapses.
- **`set3dListenerParameters` takes 12 positional doubles** — no named arguments, and the source setters are positional too. Don't pass named args copied from the web docs; it won't compile.
- **`play3d` returns `SoundHandle` synchronously.** No `await`. The async part is `init()` and `loadAsset`/`loadFile`.
- **Handle lifetime:** once a non-looping sound finishes, its handle is invalid; per-source setters on a dead handle silently do nothing. Track `sound.soundEvents` / `allInstancesFinished` if you reuse handles.
- **Voice limit:** default max active voices is 16 (`setMaxActiveVoiceCount` to change). At the limit, the oldest instance of the same sound is stolen; if none of that sound is playing, the new play silently produces a handle addressing no voice.
- **`play3d` start is buffer-quantized.** Rapidly triggered SFX (gunfire) clump to buffer boundaries — use `play3dClocked` (or `play3dScheduled` with `getEngineTime()`) for sample-accurate spacing.
- **Doppler scale mismatch:** `set3dSoundSpeed` default 343 assumes meters. If your world is in pixels or generic units, audible Doppler warble at game speeds means your velocities are huge relative to 343 — raise the sound speed or scale down.
- **Unit consistency:** attenuation distances, listener/source positions, and sound speed all share the same implicit unit. Mixing meters for distances and pixels for positions gives nonsense falloff.

## More depth

- Working test exercising the full API (listener, source, attenuation, Doppler, sound speed): `example/tests/tests/three_d_audio.dart`. The example app (`example/lib/`) has no dedicated 3D demo screen.
- Upstream SoLoud 3D concepts (attenuation models, Doppler math): https://solhsa.com/soloud/concepts3d.html

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

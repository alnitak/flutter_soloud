---
name: flutter_soloud-synthesis
version: 1
description: Teaches how to generate audio without assets in flutter_soloud using loadWaveform() with the 9 WaveForm oscillators, runtime setWaveform*() tweaks, speechText() (built-in robotic TTS that creates and plays a source), and SoLoudTools.createNotes(). Use when the user asks for a tone generator, beeps/alerts, a keyboard/instrument, synthesized sound effects, or text-to-speech without audio files.
---

# Synthesis: waveforms and speech without assets

flutter_soloud can synthesize audio with zero files. `SoLoud.instance.loadWaveform()` creates an `AudioSource` from an oscillator (not a stream to "set" — there is no `setSource`/`setUrl` like audioplayers; you create a source, then `play()` it). The returned `AudioSource` is reused and disposed exactly like one loaded from an asset. Runtime tweaks (`setWaveform*`) act on the **source**, so they affect every playing instance of it.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> beep() async {
  if (!SoLoud.instance.isInitialized) {
    await SoLoud.instance.init();
  }

  final tone = await SoLoud.instance.loadWaveform(
    WaveForm.sin, // oscillator type
    false,        // superWave
    1.0,          // scale
    0.0,          // detune
  );
  SoLoud.instance.setWaveformFreq(tone, 440); // Hz

  final handle = SoLoud.instance.play(tone); // sync, returns SoundHandle

  // ... later
  await SoLoud.instance.stop(handle);
  await SoLoud.instance.disposeSource(tone); // required: it's an AudioSource
}
```

## The API shape

### Loading

```dart
Future<AudioSource> loadWaveform(
  WaveForm waveform,
  bool superWave,
  double scale,
  double detune,
)
```

Positional args, all required. `superWave` combines several detuned oscillators for a fatter sound; `scale` and `detune` only matter when `superWave` is true. Throws `SoLoudNotInitializedException` before `init()`.

### WaveForm enum (exact names, `lib/src/enums.dart`)

`square`, `saw`, `sin`, `triangle`, `bounce` (abs(sin)), `jaws` (quarter sine, rest quiet), `humps` (half sine, rest quiet), `fSquare` ("Fourier" square, less noisy), `fSaw` ("Fourier" saw, less noisy).

Note the casing: `fSquare` / `fSaw` — not `fsquare`/`fsaw` (the upstream docs page lists them lowercase; the code is authoritative).

### Runtime tweaks (all `void`, all take the `AudioSource`, not the handle)

| Method | Effect |
| --- | --- |
| `setWaveform(AudioSource sound, WaveForm newWaveform)` | switch oscillator type |
| `setWaveformFreq(AudioSource sound, double newFrequency)` | frequency in Hz |
| `setWaveformSuperWave(AudioSource sound, bool superwave)` | toggle super wave |
| `setWaveformScale(AudioSource sound, double newScale)` | super-wave scale |
| `setWaveformDetune(AudioSource sound, double newDetune)` | super-wave detune |

These change live playback — call them on slider changes; no need to reload or stop.

### Speech

```dart
AudioSource speechText(String textToSpeech)
```

Synchronous, and it **creates AND immediately plays** the source — don't call `play()` on it. The voice is SoLoud's own built-in formant synth: deliberately retro/robotic, not the platform's system TTS (no `flutter_tts`-style voice/rate/pitch controls, no per-platform quality differences).

### Note bank helper

```dart
static Future<List<AudioSource>> SoLoudTools.createNotes({
  int octave = 3,               // asserted 0..4
  WaveForm waveForm = WaveForm.sin,
  bool superwave = true,
})
```

Returns 12 `AudioSource`s (the chromatic notes starting at `55 Hz * 2^octave`) ready to `play()`. Each is a separate source — dispose all 12 when done. Typical keyboard pattern:

```dart
final notes = await SoLoudTools.createNotes(octave: 2);
SoLoud.instance.play(notes[4]);                       // key pressed
await SoLoud.instance.stop(notes[4].handles.first);   // key released
```

## Recipe: tone generator / instrument

Model on `example/lib/waveform/waveform.dart`:

1. `await SoLoud.instance.init()` once; `SoLoud.instance.deinit()` in the owning widget's `dispose()`.
2. Create the source with `loadWaveform`, then `play()` it and keep both `AudioSource` and `SoundHandle` in state.
3. Wire UI controls straight to `setWaveform*` on the source while it plays.
4. On stop: `await SoLoud.instance.stop(handle)`, and dispose the source when it's no longer needed (`await SoLoud.instance.disposeSource(source)`).

## Traps

- **`play()` is synchronous and returns `SoundHandle`** — no `await`, and unlike just_audio there is no `setUrl`/`AudioPlayer` object; the `SoLoud` singleton is the whole player.
- **Every synthesis call requires `init()` first** and throws `SoLoudNotInitializedException` otherwise. `init()` is async; `isInitialized` lets you check synchronously.
- **Waveform/speech sources are `AudioSource`s like file-based ones.** They are NOT auto-freed when playback ends: call `disposeSource(source)` (or `disposeAllSources()`), or let `deinit()` clean everything up. Creating a new `loadWaveform` per beep without disposing leaks.
- **`speechText()` plays immediately.** Calling `play()` on its returned source is wrong. Dispose it like any other source when finished (see `example/tests/tests/speech_text.dart`).
- **`setWaveformScale`/`setWaveformDetune` are no-ops in practice unless super wave is on** (`loadWaveform(..., superWave: true, ...)` or `setWaveformSuperWave(sound, true)`). The docs' own demo disables those sliders when super wave is off.
- **`setWaveform*` targets the source, not a handle** — it retunes all currently playing instances of that source. For per-note pitch, create one source per note (`SoLoudTools.createNotes`) instead of retuning one shared source.
- **`createNotes` asserts `octave` in 0..4** and each call creates 12 sources — dispose them all, and don't call it repeatedly per key press; build the bank once.
- **No envelopes/ADSR built in.** Raw oscillators drone until stopped. Fade or stop handles yourself (`stop(handle)`, or `fadeVolume(handle, 0, duration)` then stop) to avoid clicks and endless tones.
- **Square/saw are loud and harsh** — consider lowering `play(..., volume: ...)` or using the `fSquare`/`fSaw` filtered variants for smoother output.

## More depth

- Demo: `example/lib/waveform/waveform.dart` — full tone generator UI (superWave/scale/detune sliders, all 9 `WaveForm` buttons).
- Tests with idiomatic usage: `example/tests/tests/create_notes.dart`, `example/tests/tests/speech_text.dart`, `example/tests/tests/waveform_controls.dart`.
- Real signatures: `loadWaveform`/`setWaveform*`/`speechText` at `lib/src/soloud.dart:2076-2195`; `WaveForm` at `lib/src/enums.dart:290-317`; `SoLoudTools.createNotes` in `lib/src/tools/soloud_tools.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

---
name: flutter-soloud-synthesis
version: 4
description: Teaches how to generate audio without assets in flutter_soloud using loadWaveform() with the 9 WaveForm oscillators, runtime setWaveform*() tweaks, speechText() (built-in robotic TTS that creates and plays a source), and SoLoudTools.createNotes(). Use when the user asks for a tone generator, beeps, alerts, UI ticks, countdown or timer sounds, placeholder sound effects before the real assets exist, a keyboard/piano/instrument, a tuner reference tone, synthesized sound effects, or robot-style text-to-speech without audio files, and when code loads a new waveform for every beep or stops a tone abruptly.
---

# Synthesis: waveforms and speech without assets

flutter_soloud can synthesize audio with zero files. `SoLoud.instance.loadWaveform()` creates an `AudioSource` from an oscillator (not a stream to "set" — there is no `setSource`/`setUrl` like audioplayers; you create a source, then `play()` it). The returned `AudioSource` is reused and disposed exactly like one loaded from an asset. Runtime tweaks (`setWaveform*`) act on the **source**, so they affect every playing instance of it. For custom sample-by-sample synthesis, polyphonic ADSR synths, drum machines, or C/Wasm DSP generators, see `setRealtimeStream` in **flutter-soloud-realtime-dsp** (`skills/flutter-soloud-realtime-dsp/SKILL.md`) and `createCustomAudioSource` in **flutter-soloud-custom-dsp** (`skills/flutter-soloud-custom-dsp/SKILL.md`).

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

Positional args, all required. `superWave` combines several detuned oscillators for a fatter sound; `scale` and `detune` only matter when `superWave` is true. Throws `SoLoudNotInitializedException` before `init()` and `ArgumentError` if `scale` or `detune` is NaN or infinite. Set the frequency with `setWaveformFreq` right after loading.

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

Synchronous, and it **creates AND immediately plays** the source, so no `play()` is needed to hear it. Each call creates a new, independent `AudioSource`; `play()` on it says the text again. Throws `SoLoudFailedToStartPlaybackCppException` if the engine could not create the voice (no source is created then). The voice is SoLoud's own built-in formant synth: deliberately retro/robotic, not the platform's system TTS (no `flutter_tts`-style voice/rate/pitch controls, no per-platform quality differences).

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

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| Beeps, UI ticks, countdown or timer sounds, error/success tones, a tuner reference, or placeholder SFX while the real assets do not exist yet, about to be added as small audio files | `loadWaveform(WaveForm.sin, false, 1, 0)` + `setWaveformFreq(source, hz)`, one source per tone kept for the app's lifetime | No asset to ship, no file decode, and pitch/timbre are tweakable in code. It is a raw oscillator: fine for functional sounds, not a replacement for designed effects. |
| `loadWaveform` (or `createNotes`) called inside the tap/tick handler, a new source per beep | Create the sources once (startup or first use), keep the `AudioSource`s, call `play()` many times; dispose them when the feature is closed | `play()` is synchronous and cheap, while loading is async and every undisposed source stays in memory. |
| A tone ended with a bare `stop(handle)` or started at full volume, with audible clicks | `SoLoud.instance.fadeVolume(handle, 0, const Duration(milliseconds: 20))` + `scheduleStop(handle, const Duration(milliseconds: 20))`; for the start, `play(volume: 0)` then `fadeVolume` up (**flutter-soloud-volume-pan**) | An oscillator cut mid-cycle clicks; a short engine-side ramp removes it. There is no built-in ADSR for waveform sources. |
| A pulsing alarm or tremolo made with a `Timer` toggling the volume or restarting the tone | `SoLoud.instance.oscillateVolume(handle, from, to, period)` on one looping voice | The engine modulates the volume continuously on the audio thread: steady rate, no timer jitter, one call. |
| Beep patterns, metronome clicks or sequenced notes driven by `Timer`/`Future.delayed` + `play()` | `playClocked` (live cadence) or `playScheduled` (planned times) with the waveform source (**flutter-soloud-scheduling**) | Evenly spaced, sample-accurate starts instead of timer jitter and clumping at mix-buffer boundaries. |
| A keyboard/piano built by retuning one shared source with `setWaveformFreq` per key, or with 12 recorded note files | `SoLoudTools.createNotes(octave:, waveForm:)` once: 12 sources, one per semitone, `play`/`stop` per key | Polyphony (each key is its own source) and no retuning of notes that are still sounding; no assets. |
| Envelopes (ADSR), drums, FM/wavetable synthesis, samples computed in Dart, or `setWaveformFreq` driven at high rate to "sequence" a melody | A realtime PCM stream (`setRealtimeStream`, **flutter-soloud-realtime-dsp**) or a native/Wasm custom source (**flutter-soloud-custom-dsp**) | The oscillator API has no per-note envelope or sample-accurate parameter changes; a PCM stream gives full control of every sample. |
| "Text to speech" for accessibility, natural narration or other languages | Say that `speechText` is a retro formant synth meant for English text and propose a platform TTS package; keep `speechText` (optionally with `robotizeFilter`/`lofiFilter`, **flutter-soloud-filters**) for robot or computer voices | Avoids shipping a voice the user will reject; for a robot character it is free, offline and needs no assets. |

## Traps

- **`play()` is synchronous and returns `SoundHandle`** — no `await`, and unlike just_audio there is no `setUrl`/`AudioPlayer` object; the `SoLoud` singleton is the whole player.
- **Every synthesis call requires `init()` first** and throws `SoLoudNotInitializedException` otherwise. `init()` is async; `isInitialized` lets you check synchronously.
- **Waveform/speech sources are `AudioSource`s like file-based ones.** They are NOT auto-freed when playback ends: call `disposeSource(source)` (or `disposeAllSources()`), or let `deinit()` clean everything up. Creating a new `loadWaveform` per beep without disposing leaks.
- **`speechText()` plays immediately** and returns a new source every time. Calling `play()` on it right away makes it speak twice; keep the source only to repeat the same text later, and dispose it when finished (a `speechText` call per message without `disposeSource` leaks sources).
- **`setWaveformScale`/`setWaveformDetune` are no-ops in practice unless super wave is on** (`loadWaveform(waveform, true, scale, detune)` or `setWaveformSuperWave(sound, true)`). The plugin's own demo disables those sliders when super wave is off.
- **`setWaveform*` targets the source, not a handle** — it retunes all currently playing instances of that source. For per-note pitch, create one source per note (`SoLoudTools.createNotes`) instead of retuning one shared source.
- **`createNotes` asserts `octave` in 0..4** and each call creates 12 sources — dispose them all, and don't call it repeatedly per key press; build the bank once. It always loads super-wave sources (scale 0.25, detune 1) and then applies `superwave`.
- **No envelopes/ADSR built in.** Raw oscillators drone until stopped. Fade or stop handles yourself (`fadeVolume(handle, 0, duration)` + `scheduleStop(handle, duration)`) to avoid clicks and endless tones. Both do nothing on a handle that is no longer playing.
- **Square/saw are loud and harsh** — consider lowering `play(..., volume: ...)` or using the `fSquare`/`fSaw` filtered variants for smoother output.

## More depth

- Demo: `example/lib/waveform/waveform.dart` — full tone generator UI (superWave/scale/detune sliders, all 9 `WaveForm` buttons).
- Custom DSP & Real-Time PCM synthesis demos: `example/lib/realtime_audio/realtime_pcm_synth.dart`, `example/lib/realtime_audio/isolate_drum_sequencer.dart`, `example/lib/realtime_audio/custom_dsp/` (covered in **flutter-soloud-realtime-dsp** and **flutter-soloud-custom-dsp**).
- Tests with idiomatic usage (plugin repo): `test/common/soloud_tools_test.dart`, `test/common/speech_text_test.dart`, `test/common/waveform_controls_test.dart`.
- Real signatures: `loadWaveform`/`setWaveform*`/`speechText` at `lib/src/soloud.dart`; `WaveForm` at `lib/src/enums.dart`; `SoLoudTools.createNotes` in `lib/src/tools/soloud_tools.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

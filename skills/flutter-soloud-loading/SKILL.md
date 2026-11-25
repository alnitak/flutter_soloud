---
name: flutter-soloud-loading
version: 4
description: Teaches how to load audio into flutter_soloud via loadAsset/loadMem/loadFile/loadUrl and joinTwoSources, choose between LoadMode.memory and LoadMode.disk, and manage the AudioSource lifecycle (disposeSource, autoDispose, allInstancesFinished). Use when a user asks to load or play an audio file, asset, URL, or byte buffer, to preload game sound effects (gunshots, footsteps, UI clicks) or background music, to fix load errors, "no sound" bugs or sounds that start late because they are loaded when played, to handle web platform constraints, or to clean up/dispose loaded sounds.
---

Loading in flutter_soloud is a two-step model, unlike audioplayers/just_audio: you first *load* bytes into the SoLoud C++ engine and get an `AudioSource` handle, then `play()` it any number of times, and finally *dispose* it yourself. There is no `setUrl`/`AudioPlayer` object that owns a resource for you — the `AudioSource` is just a hash (`soundHash`) referencing native memory until you dispose it.

## Minimal example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

Future<void> main() async {
  await SoLoud.instance.init();

  final source = await SoLoud.instance.loadAsset(
    'assets/sound.mp3',
    autoDispose: true, // disposed automatically when playback finishes
  );

  final handle = SoLoud.instance.play(source);
  // One source, many concurrent instances:
  final handle2 = SoLoud.instance.play(source);

  // Without autoDispose, dispose manually when the sound is no longer needed
  // (this also stops its playing handles):
  // await SoLoud.instance.disposeSource(source);
}
```

## The API shape

All loaders are on `SoLoud.instance` and require `await SoLoud.instance.init()` first (otherwise every loader throws `SoLoudNotInitializedException`).

- `Future<AudioSource> loadAsset(String key, {LoadMode mode = LoadMode.memory, AssetBundle? assetBundle, bool autoDispose = false})` — `key` is an asset key like `'assets/sound.mp3'` (NOT a file path). On native, the asset is copied to a temp file and loaded from there. On Web it reads the bundle bytes and delegates to `loadMem`, so it works everywhere.
- `Future<AudioSource> loadMem(String path, Uint8List buffer, {LoadMode mode = LoadMode.memory, bool autoDispose = false})` — `buffer` is the bytes of a *supported audio file* (any format listed below), not raw PCM. `path` is only a reference name to distinguish this buffer (e.g. `'my_sound.mp3'`), not a real filesystem path. The only loader usable on Web (and the one `loadAsset`/`loadUrl` delegate to there); on Web `mode` is ignored and data is fed to the engine in chunks, yielding to the event loop to keep the UI responsive. Bytes that cannot be decoded throw `SoLoudFileLoadFailedException` on every platform.
- `Future<AudioSource> loadFile(String path, {LoadMode mode = LoadMode.memory, bool autoDispose = false})` — absolute filesystem path. NOT available on Web: it throws `UnimplementedError` there (use `loadMem` instead).
- `Future<AudioSource> loadUrl(String url, {LoadMode mode = LoadMode.memory, http.Client? httpClient, bool autoDispose = false})` — downloads the file, then loads it (temp file on native, `loadMem` on Web). Non-`200` status throws `SoLoudNetworkStatusCodeException`. Pass your own `http.Client` when loading many URLs at startup — otherwise a new client is created per call. On Web the request is subject to normal CORS rules.
- `Future<AudioSource> joinTwoSources(String path, Uint8List bufferLeft, Uint8List bufferRight, {bool autoDispose = false})` — builds one stereo `AudioSource` from two buffers (bytes of supported audio files). Non-mono input is converted to mono on the native side; both sides are resampled to the engine sample rate; if lengths differ, the shorter side is padded with silence to match the longer. Always loads fully into RAM (`LoadMode.memory`), no `mode` parameter.
- `Future<AudioSource> playSource({String? asset, String? file, String? url, LoadMode mode = LoadMode.disk, ...})` — convenience that loads (with `autoDispose: true`) and then plays; exactly one of `asset`/`file`/`url`. The sound starts only after the load finishes, so do not use it for sounds that must start on an event.

### LoadMode

```dart
enum LoadMode {
  memory, // decode entire file into RAM: less CPU, low latency, fast seek
  disk,   // stream chunks from the file: less RAM, more CPU
}
```

Use `memory` (the default) for sound effects and anything you seek. Use `disk` for large background music — but note that with `disk` mode, seeking lags with MP3s.

`disk` and the OS-decoded formats: M4A/MP4 files stream from disk on Android, iOS and macOS (sample-accurate seek, gapless loops). Raw AAC, AC-3 and E-AC-3 files, and M4A/MP4 on Windows and Linux, cannot be streamed from disk: they are fully decoded into memory when loaded, exactly like `memory`. On Web `mode` is ignored.

Supported formats:
- **Core formats**: MP3, WAV, OGG (Vorbis/Opus/FLAC), FLAC.
- **OS-native formats** (via platform decoders on macOS, iOS, Android, Windows, Web, Linux): M4A, MP4 (audio tracks), AAC, AC-3 (Dolby Digital), and E-AC-3 (Dolby Digital Plus). *Linux note:* On Linux, decoding these formats requires FFmpeg shared libraries (`libavcodec` and `libavformat`) installed on the host system (e.g. `sudo apt install ffmpeg libavcodec-extra`, `sudo pacman -S ffmpeg`, or `sudo dnf install ffmpeg-free`). FFmpeg 4.4 to 8 is supported; without it, loading these formats throws `SoLoudAudioFormatNotSupportedCppException`. Core formats work out of the box without FFmpeg. For an MP4 file with a video track, the first audio track is played.

> [!NOTE]
> MP4 and M4A containers are supported for file, asset, URL, and memory loading, but are **NOT supported for chunk streaming** (`setBufferStream`/`setPullBufferStream`). For real-time streaming, use elementary streams such as AAC ADTS, AC-3, E-AC-3, OGG, MP3, WAV, or raw PCM.

### AudioSource lifecycle

`AudioSource` cannot be constructed directly; loaders return it.

- `await SoLoud.instance.disposeSource(source)` — stops all handles of the source and reclaims memory. Never `play()` a disposed source.
- `await SoLoud.instance.disposeAllSources()` — disposes everything loaded (not needed at engine shutdown).
- `SoLoud.instance.isValidAudioSource(source)` — true while the source is loaded.
- `source.autoDispose` (settable property or loader parameter) — when true, the source is disposed automatically once all its handles finish. Ideal for one-shot SFX; don't use for sources you replay.
- `source.soundPath` — the parameter (file path, asset key, URL, or buffer identifier) used to load this audio source (`''` for waveforms/speech).
- `source.tempFilePath` — the path to the temporary file created on disk when loading an asset or URL on native platforms (`''` if none was created).
- `source.handles` — unmodifiable set of `SoundHandle`s of currently playing instances.
- `source.soundEvents` — broadcast stream of `({SoundEventType event, AudioSource sound, SoundHandle handle})` records. `SoundEventType` has only `handleIsNoMoreValid` (a handle finished or was stopped) and `soundDisposed`.
- `source.allInstancesFinished` — stream that fires each time the count of playing instances drops to zero; the safe-dispose signal:

```dart
source.allInstancesFinished.first.then((_) {
  SoLoud.instance.disposeSource(source);
});
SoLoud.instance.play(source);
```

### Parallel loading

Wrap loaders in `Future.wait` (or `.wait`) — measurably faster than sequential `await`s (20–40% per the docs):

```dart
final [click, music] = await [
  SoLoud.instance.loadAsset('assets/click.wav'),
  SoLoud.instance.loadAsset('assets/music.ogg', mode: LoadMode.disk),
].wait;
```

### Errors

All loaders throw `SoLoudException` subclasses (sealed base class). Catch the specific ones:

```dart
try {
  final source = await SoLoud.instance.loadUrl(url);
} on SoLoudNotInitializedException {
  // call SoLoud.instance.init() first
} on SoLoudNetworkStatusCodeException catch (e) {
  // loadUrl: non-200 HTTP status
} on SoLoudFileNotFoundException {
  // loadFile: no file at that path
} on SoLoudFileLoadFailedException {
  // unreadable, undecodable or unsupported data
} on SoLoudAudioFormatNotSupportedCppException {
  // no decoder on this platform: AAC/M4A/AC-3 on Linux without FFmpeg,
  // AC-3/E-AC-3 on Web
} on SoLoudTemporaryFolderFailedException {
  // loadAsset/loadUrl: couldn't write the temp copy
}
```

`loadAsset` with a missing asset key throws the same `FlutterError` as `AssetBundle.load` — not a `SoLoudException`.

## Proactively recommend

When the user's request or existing code matches the left column, propose the
approach on the right in one or two sentences (what it is and what they gain),
then apply it if they agree or if they asked you to decide. Do not silently
rewrite working code, and do not propose it when the simpler call is enough.

| When you see | Recommend | Why |
|---|---|---|
| `loadAsset`/`loadFile`/`loadUrl` inside a tap, fire or collision callback, or a new `AudioSource` loaded for every shot | Load each sound once at startup or level load (in parallel with `.wait`), keep the `AudioSource`, and call `play()` on it every time | `play()` is synchronous and one source plays any number of overlapping voices; loading reads and decodes the file each time, and loading the same path again makes a second copy in memory (and logs a warning). Costs the memory of the preloaded sounds |
| `playSource(asset: ...)` for a gunshot, footstep, UI click or any sound that must start on the event | Preload with `loadAsset(..., mode: LoadMode.memory)` and `play()` | `playSource` loads first, so the sound starts after the load time; it is meant for one-off playback where that lag does not matter |
| Short sound effects loaded with `LoadMode.disk`, or long background music/ambience loaded with the default `LoadMode.memory` | `LoadMode.memory` for SFX, `LoadMode.disk` for long tracks | `memory` keeps decoded PCM in RAM: lowest CPU per voice and instant start, but minutes of music take a lot of RAM. `disk` keeps RAM low at the cost of decoding while playing, and MP3 seeks lag. Ignored on web |
| `Future.delayed(length)` followed by `disposeSource`, or one-off sounds that are never disposed | `autoDispose: true` on the loader, or `source.allInstancesFinished.first.then(...)` | The source is freed when its last voice really ends (also after a `stop()`), with no timer to keep in sync. Do not use it for sounds you replay |
| Many sequential `await loadAsset(...)` calls on a loading screen | `await [loadAsset(a), loadAsset(b), ...].wait` | The loads run in parallel; the plugin docs measure it 20 to 40% faster |
| Two files (intro + loop) stitched with a timer or a completion callback for looping music | One file and `play(source, looping: true, loopingStartAt: introLength)` (**flutter-soloud-playback**) | The engine jumps back to the loop point by itself, so there is no gap or overlap at the seam and only one file to load |
| Beeps, UI ticks, metronome clicks or placeholder SFX with no audio asset yet | `loadWaveform(WaveForm.square, false, 1, 0)` and `setWaveformFreq` (**flutter-soloud-synthesis**) | Generated by the engine: no asset, no decoding, nothing added to the app size |
| A very long or remote file (podcast, audiobook, DJ set) loaded whole with `loadUrl`/`loadMem` only to play and seek it | A pull-buffer stream (**flutter-soloud-pull-streaming**); for live radio a push stream (**flutter-soloud-streaming**) | `loadUrl` downloads the whole file before returning; a pull stream starts at once and keeps memory bounded. More code, one voice per stream, no MP4/M4A |

## Traps

- **Loading is not playing.** Coming from audioplayers, `loadAsset` does not emit sound. You must still call `SoLoud.instance.play(source)`.
- **`loadMem` takes encoded file bytes, not PCM.** Passing raw PCM samples yields a load failure. (For PCM there are separate buffer-stream APIs — outside this skill's scope.)
- **`loadFile` throws `UnimplementedError` on Web.** On Web always go through `loadMem` (get bytes from `rootBundle`, `http`, or a file picker).
- **Web CORS applies to `loadUrl`.** A URL that plays fine on mobile can fail in the browser if the server doesn't send CORS headers; this surfaces as a fetch error, not a SoLoud error code.
- **AC-3 / E-AC-3 on Web:** AC-3 and E-AC-3 audio formats are **not supported on the Web platform in any browser**. Chrome and Firefox lack Dolby licensing, and Safari's WebCodecs engine rejects raw AC-3 frames with Cocoa decoding errors, while Web Audio API does not decode raw `.ac3` files. Attempting to load AC-3 or E-AC-3 on Web throws `SoLoudAudioFormatNotSupportedCppException`. For web audio, use MP3, WAV, OGG/Opus, FLAC, or AAC.
- **Temp-file eviction on mobile.** `loadAsset`/`loadUrl` copy to the OS temp directory (`SoLoudLoader-Temp-Files`). The OS may purge it between load and play. A `LoadMode.disk` sound streams from that file, so `play()` then throws `SoLoudFileNotFoundException`. If the gap between load and play can be long, prefer `LoadMode.memory` (the audio is already decoded in RAM) or `loadMem`.
- **Playing a disposed source fails.** After `disposeSource`, do not `play()` that `AudioSource` again — check with `isValidAudioSource` if unsure. Same after `autoDispose` has fired.
- **`autoDispose` + looping never fires.** A looping handle never finishes, so the source is never auto-disposed until every handle is stopped.
- **Loading the same path again loads a second copy.** The call logs a warning and returns a new, independent `AudioSource` with its own decoded data: twice the memory, and disposing one does not stop or free the other. Load once, reuse.
- **MP3 + `LoadMode.disk` seek lag.** Documented native-side behavior; use `LoadMode.memory` if you need responsive `seek()`.
- **`disposeSource` closes the event streams.** After disposal, `source.soundEvents`/`allInstancesFinished` are closed — don't add listeners to a disposed source.

## More depth

- Demos/tests in the repo: `example/tests/tests/load_mem.dart`, `example/tests/tests/join_two_sources.dart`, `example/tests/tests/auto_dispose.dart`, `example/tests/tests/all_instances_finished.dart`, `example/tests/tests/async_multi_load.dart`. The main example app loads assets in `example/lib/main.dart`.
- Loader internals (temp files, cleanup): `lib/src/utils/loader_io.dart` and `lib/src/utils/loader_web.dart`.
- Exception hierarchy: `lib/src/exceptions/exceptions.dart` and siblings.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

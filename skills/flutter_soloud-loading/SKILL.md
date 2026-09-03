---
name: flutter_soloud-loading
version: 1
description: Teaches how to load audio into flutter_soloud via loadAsset/loadMem/loadFile/loadUrl and joinTwoSources, choose between LoadMode.memory and LoadMode.disk, and manage the AudioSource lifecycle (disposeSource, autoDispose, allInstancesFinished). Use when a user asks to load or play an audio file, asset, URL, or byte buffer, to fix load errors or "no sound" bugs, to handle web platform constraints, or to clean up/dispose loaded sounds.
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

  // If not using autoDispose, dispose manually when done:
  await SoLoud.instance.disposeSource(source);
}
```

## The API shape

All loaders are on `SoLoud.instance` and require `await SoLoud.instance.init()` first (otherwise every loader throws `SoLoudNotInitializedException`).

- `Future<AudioSource> loadAsset(String key, {LoadMode mode = LoadMode.memory, AssetBundle? assetBundle, bool autoDispose = false})` — `key` is an asset key like `'assets/sound.mp3'` (NOT a file path). On native, the asset is copied to a temp file and loaded from there. On Web it reads the bundle bytes and delegates to `loadMem`, so it works everywhere.
- `Future<AudioSource> loadMem(String path, Uint8List buffer, {LoadMode mode = LoadMode.memory, bool autoDispose = false})` — `buffer` is the bytes of a *supported audio file* (MP3/WAV/OGG/FLAC), not raw PCM. `path` is only a reference name to distinguish this buffer (e.g. `'my_sound.mp3'`), not a real filesystem path. The only loader usable on Web (and the one `loadAsset`/`loadUrl` delegate to there); on Web `mode` is ignored and data is fed to the engine in chunks, yielding to the event loop to keep the UI responsive.
- `Future<AudioSource> loadFile(String path, {LoadMode mode = LoadMode.memory, bool autoDispose = false})` — absolute filesystem path. NOT available on Web (use `loadMem` instead).
- `Future<AudioSource> loadUrl(String url, {LoadMode mode = LoadMode.memory, http.Client? httpClient, bool autoDispose = false})` — downloads the file, then loads it (temp file on native, `loadMem` on Web). Non-`200` status throws `SoLoudNetworkStatusCodeException`. Pass your own `http.Client` when loading many URLs at startup — otherwise a new client is created per call. On Web the request is subject to normal CORS rules.
- `Future<AudioSource> joinTwoSources(String path, Uint8List bufferLeft, Uint8List bufferRight, {bool autoDispose = false})` — builds one stereo `AudioSource` from two buffers (bytes of supported audio files). Non-mono input is converted to mono on the native side; both sides are resampled to the engine sample rate; if lengths differ, the shorter side is padded with silence to match the longer. Always loads fully into RAM (`LoadMode.memory`), no `mode` parameter.

### LoadMode

```dart
enum LoadMode {
  memory, // decode entire file into RAM: less CPU, low latency, fast seek
  disk,   // stream chunks from the file: less RAM, more CPU
}
```

Use `memory` (the default) for sound effects and anything you seek. Use `disk` for large background music — but note that with `disk` mode, seeking lags with MP3s.

Supported formats: MP3, WAV, OGG (Vorbis/Opus/FLAC), FLAC.

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
  // unreadable or unsupported format
} on SoLoudTemporaryFolderFailedException {
  // loadAsset/loadUrl: couldn't write the temp copy
}
```

`loadAsset` with a missing asset key throws the same `FlutterError` as `AssetBundle.load` — not a `SoLoudException`.

## Traps

- **Loading is not playing.** Coming from audioplayers, `loadAsset` does not emit sound. You must still call `SoLoud.instance.play(source)`.
- **`loadMem` takes encoded file bytes, not PCM.** Passing raw PCM samples yields a load failure. (For PCM there are separate buffer-stream APIs — outside this skill's scope.)
- **`loadFile` throws/is unsupported on Web.** On Web always go through `loadMem` (get bytes from `rootBundle`, `http`, or a file picker).
- **Web CORS applies to `loadUrl`.** A URL that plays fine on mobile can fail in the browser if the server doesn't send CORS headers; this surfaces as a fetch error, not a SoLoud error code.
- **Temp-file eviction on mobile.** `loadAsset`/`loadUrl` copy to the OS temp directory (`SoLoudLoader-Temp-Files`). The OS may purge it between load and play, causing a rare crash. If the gap between load and play can be long, prefer `loadMem` with `LoadMode.memory`.
- **Playing a disposed source fails.** After `disposeSource`, do not `play()` that `AudioSource` again — check with `isValidAudioSource` if unsure. Same after `autoDispose` has fired.
- **`autoDispose` + looping never fires.** A looping handle never finishes, so the source is never auto-disposed until every handle is stopped.
- **Re-loading the same path is a no-op.** The second call logs a warning and returns a source for the already-loaded sound. Load once, reuse.
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

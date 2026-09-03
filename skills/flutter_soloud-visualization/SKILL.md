---
name: flutter_soloud-visualization
version: 1
description: Teaches flutter_soloud's two visualization paths — live FFT/wave data via setVisualizationEnabled + the audioVisualizationEvents stream, and offline waveform extraction via readSamplesFromFile/readSamplesFromMem (plus getApproximateVolume for VU meters). Use when the user asks for a spectrum/waveform visualizer, audio-reactive UI, VU/level meter, or a static waveform preview of an audio file.
---

# Visualization in flutter_soloud

flutter_soloud has two unrelated ways to get drawable sample data out of the SoLoud engine:

1. **Live visualization** — the engine emits `AudioVisualizationData` packets (waveform and/or FFT magnitudes of the *mixed output*) on a broadcast stream, at mixer rate. For real-time spectrum/waveform widgets.
2. **Offline waveform reading** — `readSamplesFromFile` / `readSamplesFromMem` decode an audio file in a background isolate and return a fixed number of equally-spaced samples. For static waveform previews (SoundCloud-style).

Pick the right one. The live stream cannot give you the waveform of a not-yet-played file; the offline readers cannot follow playback.

## Minimal example (live FFT + wave)

```dart
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

class Visualizer extends StatefulWidget {
  const Visualizer({super.key});
  @override
  State<Visualizer> createState() => _VisualizerState();
}

class _VisualizerState extends State<Visualizer> {
  StreamSubscription<AudioVisualizationData>? _sub;
  AudioVisualizationData? _data;

  @override
  void initState() {
    super.initState();
    _init();
  }

  Future<void> _init() async {
    await SoLoud.instance.init(); // must complete first
    SoLoud.instance.setVisualizationEnabled(
      true,
      windowSize: 256,
      kind: VisualizationKind.waveAndFft,
      channel: VisualizationChannel.merged,
    );
    SoLoud.instance.setFftSmoothing(0.8);
    _sub = SoLoud.instance.audioVisualizationEvents.listen((data) {
      if (mounted) setState(() => _data = data);
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final fft = _data?.fftData; // Float32List? of windowSize/2 magnitudes
    if (fft == null) return const SizedBox(height: 100);
    return CustomPaint(
      size: const Size(double.infinity, 100),
      painter: BarsPainter(fft),
    );
  }
}

class BarsPainter extends CustomPainter {
  BarsPainter(this.fft);
  final Float32List fft;

  @override
  void paint(Canvas canvas, Size size) {
    final barWidth = size.width / fft.length;
    final paint = Paint()..strokeWidth = barWidth * 0.8;
    for (var i = 0; i < fft.length; i++) {
      final h = size.height * fft[i].clamp(0.0, 1.0);
      canvas.drawLine(
        Offset(i * barWidth + barWidth / 2, size.height),
        Offset(i * barWidth + barWidth / 2, size.height - h),
        paint,
      );
    }
  }

  @override
  bool shouldRepaint(BarsPainter oldDelegate) => true; // data changes every event
}
```

## Minimal example (offline waveform)

```dart
import 'package:flutter/foundation.dart';
import 'package:flutter_soloud/flutter_soloud.dart';

// No SoLoud.instance.init() needed — the readers run their own decoder
// inside a `compute` isolate.
Future<Float32List> waveformBars(String path, Uint8List bytes, int bars) {
  if (kIsWeb) {
    // readSamplesFromFile does not exist on Web; feed it the bytes.
    return SoLoud.instance.readSamplesFromMem(bytes, bars, average: true);
  }
  return SoLoud.instance.readSamplesFromFile(path, bars, average: true);
}
```

A common choice for `bars` is the widget's pixel width (`MediaQuery.sizeOf(context).width.toInt() * 4` in the plugin's example). Draw each value as a vertical line centered on the middle (see `example/lib/wave_data/wave_data.dart`).

## The API shape

All on `SoLoud.instance` (singleton), imported from `package:flutter_soloud/flutter_soloud.dart`.

### Live

- `void setVisualizationEnabled(bool enabled, {int windowSize = 256, VisualizationKind kind = VisualizationKind.waveAndFft, int channel = VisualizationChannel.merged})` — turns the analyzer on/off. Must be called after `init()`; throws `SoLoudNotInitializedException` otherwise, `SoLoudCppException` on bad params.
- `Stream<AudioVisualizationData> get audioVisualizationEvents` — broadcast stream; emits one packet per mixer buffer while enabled.
- `AudioVisualizationData` — `channelCount`, `wave` (`List<Float32List>`, one per channel, length `windowSize`, values `[-1.0, 1.0]`), `fft` (`List<Float32List>`, one per channel, length `windowSize / 2`, magnitudes `[0.0, 1.0]`). Either list is **empty** if its kind is disabled. `waveData` / `fftData` are nullable convenience getters for `wave.first` / `fft.first`.
- `VisualizationKind` — real enum: `wave`, `fft`, `waveAndFft`.
- `VisualizationChannel` — **not an enum**, an `abstract final class` of int constants: `merged = -1` (downmix to mono), `all = -2` (per-channel lists), or pass any 0-based channel index (`0` = left). There is no `VisualizationChannel.values`; don't switch over it.
- `void setFftSmoothing(double smooth)` — exponential decay on falling FFT bins, `0.0`–`1.0`. Rise is never smoothed.
- `double getApproximateVolume(int channel)` — instantaneous overall output volume per *output* channel (speaker). Poll it on a `Timer` for a VU meter. Returns 0 for invalid channels.

### Offline

- `Future<Float32List> readSamplesFromFile(String completeFileName, int numSamplesNeeded, {double startTime = 0, double endTime = -1, bool average = false})` — native platforms only.
- `Future<Float32List> readSamplesFromMem(Uint8List buffer, int numSamplesNeeded, {double startTime = 0, double endTime = -1, bool average = false})` — everywhere; `buffer` is the encoded file bytes (mp3/wav/flac/ogg, from assets, `File.readAsBytes()`, or a network fetch).
- Times are in **seconds**; `endTime: -1` means "to end of file". `average: true` makes each returned value the mean of its time bucket instead of a single point sample — smoother waveforms, negligible cost.

### Divergences from audioplayers / just_audio / web Audio API assumptions

- No `AudioPlayer`-style per-sound analyser. Live visualization taps the **whole mixed engine output** (all voices, post-filter), not an individual track.
- There is no `onPositionChanged`-style callback to hook FFT onto; you enable the analyzer once and consume a stream.
- `getApproximateVolume` is per **output channel**, not per voice — there is no built-in per-sound level meter.
- The offline readers don't require `init()` and don't need the sound loaded via `loadMem`/`loadAsset` first — they decode independently (asset bytes via `rootBundle.load` are fine).

## Traps

- **Silent stream.** `audioVisualizationEvents` emits nothing until `setVisualizationEnabled(true)` is called, and calling it before `init()` completes throws. If your painter stays black, check enablement and init order first.
- **`windowSize` must be a power of two in 128–8192.** Anything else fails with `SoLoudCppException`, not an assertion. FFT bin count is `windowSize / 2`, wave length is `windowSize` — don't assume they're equal.
- **Empty lists by kind.** With `kind: VisualizationKind.fft`, `data.wave` is empty and `data.waveData` is `null` — null-check the convenience getters.
- **Every event is a fresh copy.** The binding copies native memory into new `Float32List`s per packet (`Float32List.fromList`), so retaining an event is safe and cheap to reason about — but don't hold a queue of them unbounded. Note: older docs mention `wavePointer`/`fftPointer` getters on `AudioVisualizationData`; they do not exist in this version of the package.
- **Event rate is tied to `bufferSize`, not frames.** With the default `bufferSize: 2048` at 44.1 kHz you get ~21 packets/s; the example uses `init(bufferSize: 1024)` (~43/s) for smoother visuals. Don't do heavy layout per event; a `CustomPainter` repaint is fine.
- **`deinit()` disables visualization** (and `deinit()` is called when `init()` re-initializes). Re-enable after re-init.
- **`readSamplesFromFile` throws/is unavailable on Web** — gate on `kIsWeb` and use `readSamplesFromMem` with the file bytes. On Web, `readSamplesFromMem` runs synchronously (no real isolate) and can jank the UI for large files.
- **Returned length ≠ `numSamplesNeeded`.** The list is shorter if `endTime` overshoots the file duration. Size your painter from `data.length`, never from the requested count.
- **Asserts on time range:** `startTime >= 0` and `endTime > startTime` (unless `-1`). Passing a `Duration` won't compile — convert with `duration.inMilliseconds / 1000`.
- **Per-channel indexing is by mixer output channels.** With `channel: VisualizationChannel.all`, `data.wave[0]` is left, `[1]` is right — the number of entries equals the engine's `Channels` setting at `init()`, not the file's channel count.

## More depth

- [references/live_painter_recipe.md](references/live_painter_recipe.md) — full per-channel wave + FFT `CustomPainter` widget, adapted from the plugin example.
- [references/offline_waveform.md](references/offline_waveform.md) — full `readSamplesFromMem` flow with file picking, averaging, and the waveform painter.
- Live demo: `example/lib/audio_data/audio_data.dart` (+ `data_widget.dart`). Offline demo: `example/lib/wave_data/wave_data.dart`.

## Keeping this skill current

This skill ships inside the flutter_soloud package, so upgrading flutter_soloud can carry a newer revision of it than the copy installed in the project. To check, run:

```sh
dart run flutter_soloud:skills --check
```

It reports the installed and bundled skill versions and exits non-zero when an update is available. Offer to update with `dart run flutter_soloud:skills` (which touches only the skills, never pubspec or build files).

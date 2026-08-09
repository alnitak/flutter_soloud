A low-level audio plugin for Flutter.

[![Pub Version](https://img.shields.io/pub/v/flutter_soloud?logo=dart)](https://pub.dev/packages/flutter_soloud)
[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

||Linux|Windows|Android|macOS|iOS|Web|
|-|:-:|:-:|:-:|:-:|:-:|:-:|
|Support|💙|💙|💙|💙|💙|💙|
|Minimum Version|Any|Any|21+|10.15+|13.0+|iOS 16.4+<br>Safari 16.4+<br>Chrome 91+<br>Edge 91+<br>Firefox 89+|

## Overview

A high-performance audio plugin designed primarily for games and immersive applications, providing low latency and advanced features.

## Key Features

- ⚡ Low latency, high performance audio
- ⏱️ Sample-accurate scheduled playback: `playClocked` for sub-millisecond spaced playback regardless of buffer size, and `playScheduled` for score/manifest-style scheduling of whole batches of sounds on the engine's own clock (with optional scheduled stop/fade). Perfect for metronomes, music sequencers, rhythm games and precisely timed audio cues
- 🎮 3D positional audio with Doppler effect
- 🔄 Gapless looping with half-open `[start, end)` loop regions
- 🔄 Stream audio with auto-pause for buffering, support for PCM, MP3, WAV, Ogg with Opus, Vorbis and FLAC containers
- 📥 Pull-buffer streaming: the engine requests encoded data on demand (MP3, WAV, FLAC, Ogg Opus/Vorbis/FLAC), with seek support and callbacks for buffering, metadata, duration and data requests — ideal for network streams and custom data sources
- 🚌 Mixing buses: group voices (music, SFX, UI...) into sub-mixes with their own volume, filters and visualization
- 📊 Get audio wave and/or FFT audio data in real-time (useful for visualization)
- 🎛️ Rich effects system (reverb, echo, limiter, parametric equalizer, pitch shift, etc.)
- ⚙️ Faders for attributes (e.g. fade out for 2 seconds, then stop)
- 🎚️ Oscillators for attributes
- 🌊 Waveform generation and visualization
- 🔊 Multiple voices, playing different or even the same sound multiple times
- 🎵 Support for MP3, WAV, OGG, and FLAC
- 🔴 Capture the master mixer output as a stream for recording, processing, or streaming (with different PCM formats and Opus, Vorbis, FLAC, WAV encoded stream formats)
- ⏱️ Read audio data samples from a file with a given time range
- 🌊 Generate waveforms in real-time with various types (sine, square, saw, triangle, etc.)

Whether you are building a game (3D positional SFX, mixing buses, low-latency playback) or any other kind of audio app — music tools, metronomes, radio/streaming apps, visualizers, recorders — the plugin exposes the low-level control you need.

## Getting Started
- Watch the Flutter [Package of the Week](https://www.youtube.com/watch?v=2t6Bt04EyLw) video.
- Especially for web use, please look at the [setup guide docs](https://docs.page/alnitak/flutter_soloud_docs/get_started/setup).

If you are looking for a package to visualize audio using shaders or CustomPainter, please check out [audio_flux](https://pub.dev/packages/audio_flux). It uses this plugin for output and [flutter_recorder](https://pub.dev/packages/flutter_recorder) for input.

Also, if you are building using Swift Package Manager (SPM), please check out [iOS and macOS Configuration](https://docs.page/alnitak/flutter_soloud_docs/get_started/setup#ios-and-macos-configuration).

### Android: FlutterEngine lifecycle

The native engine is process-global, while the Dart isolate driving it belongs
to one FlutterEngine. On Android the plugin observes that engine's lifecycle so
the two cannot drift apart:

- **Hot restart** retires the callbacks belonging to the discarded isolate, so
  nothing calls into it — including the per-stream buffering, metadata and
  data-request callbacks that the audio thread drives. The engine stays up and
  the new isolate's `init()` replaces it as usual.
- **FlutterEngine destroyed** (a cached engine behind `audio_service` being
  disposed, an add-to-app host destroying an engine) tears down the player, the
  output device and the scheduler that engine owned, even if your Dart code
  never got to call `deinit()`. The blocking part runs on a native worker
  thread, so the platform thread is never held.
- **Activity recreation is not engine destruction.** A cached FlutterEngine
  deliberately outlives its Activity; rotating the screen or recreating the
  Activity tears nothing down.

None of this needs any setup: the plugin registers itself, and does no native
work at all until one of those transitions happens.

**Supported scope:** one active FlutterEngine at a time, including replacing it
with a new one. Two FlutterEngines using the plugin *simultaneously* is not
supported — the engine they would share is process-global, and the last one to
initialize wins.

### iOS and macOS: FlutterEngine lifecycle

Both get the same native teardown, with honest differences in timing.

- **FlutterEngine deallocated** tears down the player, output device and
  scheduler that engine owned, and cannot tear down an engine that has since
  replaced it. The blocking part runs on a native worker, so the platform
  thread is never held.
- **Callback retirement is not guaranteed to happen before the isolate goes.**
  Android can retire callbacks while the engine is still valid
  (`onEngineWillDestroy`) and before a hot restart (`onPreEngineRestart`).
  Flutter's public iOS plugin API offers neither: the only hook is plugin
  detach during `FlutterEngine` deallocation, and a hot restart gives no hook at
  all. So on iOS there is a window — between the old isolate going away and
  either detach arriving or the next `init()` running — in which a native
  callback belonging to the departed isolate has not yet been retired. Recovery
  happens at the next initialization, which retires the stale registrations
  before claiming afresh. We do not close this with private Flutter APIs.

On **macOS** the difference is sharper still. Its plugin API has no detach hook
of any kind — the protocol is only `registerWithRegistrar:` and
`handleMethodCall:result:` — so the plugin takes its own deallocation as the
signal that the engine has gone. That works because FlutterEngine's `dealloc`
releases the references holding the plugin, but it is reference-count timing
rather than a documented contract: an app that retains the plugin instance (via
`valuePublishedByPlugin:`, say) delays or prevents it, and the result is simply
that the engine is not released automatically, exactly as before this existed.

Arming this needs one round trip to the platform thread, because neither Apple
platform exposes the engine's identity to a plugin the way Android does. If Flutter's
messaging is not available — for instance `SoLoud.init()` called without
`WidgetsFlutterBinding.ensureInitialized()`, which this package has never
required and still does not — initialization proceeds exactly as before and
logs a warning that automatic teardown was not armed. An explicit `deinit()`,
and recovery at the next `init()`, keep working either way.

## Documentation

- [Full Documentation](https://docs.page/alnitak/flutter_soloud_docs)
- [API Reference](https://pub.dev/documentation/flutter_soloud/latest/)

## Simple Example

```dart
import 'package:flutter_soloud/flutter_soloud.dart';

void example() async {
  final soloud = SoLoud.instance;
  await soloud.init();

  await soloud.playSource(asset: 'assets/sound.mp3');
  // or
  final sound = await soloud.loadAsset('assets/sound.mp3');
  final handle = soloud.play(sound);
  
  [...]

  soloud.deinit();
}
```

## Apps & Games Using flutter_soloud

A showcase of apps and games built with this plugin:

| App/Game | Developer | Description |
|----------|-----------|-------------|
| GPhil</br>[web](https://app.g-phil.app/) [macOS](https://apps.apple.com/it/app/gphil/id6740543718) [Windows](https://apps.microsoft.com/detail/9pkkz2p2dldg?ocid=webpdpshare)| Vyacheslav Gryaznov | Innovative app designed for musicians to play instrumental concertos with flexible virtual orchestral accompaniment.|
| [Forcebar](https://forcebar.xyz) | Doug Todd | Forcebar is a pure reflex game. |
| [RadioVisualizer](https://radiovisualizer.com) | Marco Bavagnoli | Stream over 35,000 live radio stations from every corner of the globe. |
| Stellar Bastion</br>[web](https://www.crazygames.com/game/stellar-bastion) [Android](https://play.google.com/store/apps/details?id=com.coconutisland.stellar_bastion) [iOS](https://apps.apple.com/us/app/stellar-bastion/id6761073618) | Coconut Island Apps | 2D Tower Defense game. |
| Mortigen</br>[web](https://koldo92.github.io/mortigen/) [Android](https://play.google.com/store/apps/details?id=com.ler.mortigen) [iOS](https://apps.apple.com/us/app/mortigen/id6761758806) | Luis Enrique Ruiz | Roguelite survival shooter. |
| SUMOJI</br>[web](https://straspool.eu/sumoji/) [Android](https://play.google.com/store/apps/details?id=eu.straspool.sumoji) [iOS](https://apps.apple.com/us/app/sumoji/id6751641875) | Valentin Martinet | Fun Emoji-based Sudoku. |
| GuanDan</br>[web](https://guandan.app/) [macOS](https://apps.apple.com/us/app/%E6%8E%BC%E8%9B%8B-guandan/id6757966323) [Windows](https://apps.microsoft.com/detail/9pbv1xp2lc50) [Android](https://play.google.com/store/apps/details?id=org.rockstudio.guandan) [iOS](https://apps.apple.com/us/app/%E6%8E%BC%E8%9B%8B-guandan/id6757966323) | [yangyuan](https://github.com/yangyuan) | GuanDan (掼蛋) is a popular four-player Chinese card game. |

*Want to add your app? Feel free to open a PR!*

## License

The Dart plugin is covered by the MIT license. For information about the underlying SoLoud engine license, see the [documentation](https://docs.page/alnitak/flutter_soloud/get_started/license).

# Plan: Agent skills for flutter_soloud (`dart run flutter_soloud:skills`)

## Goal

Ship AI-agent "skills" (task-oriented `SKILL.md` instruction files) inside the
`flutter_soloud` package, one per feature area, so that users can run
`dart run flutter_soloud:skills` in their project and get all skills installed
into their agent skill homes (`.claude/skills`, `.cursor/skills`, etc.).

Modelled on flutter_scene's implementation
(`packages/flutter_scene/bin/skills.dart` +
`lib/src/fmat/init_command.dart`, skills under `packages/flutter_scene/skills/`).

## Sources for skill content (all verified during exploration)

- `lib/src/soloud.dart` (~4900 lines) — full `SoLoud` API with doc comments.
- `/Volumes/NVME/workspace/flutter_soloud_docs/docs/**` — 20 `.mdx` doc pages
  (setup, loading, playback, volume, 3d, waveforms, push/pull streaming,
  mixing bus, filters, mixer output capture, FFT/wave visualization, web
  notes, audio_context, no_xiph_libs).
- `example/lib/**` — runnable demos per feature, each mapped to a skill:
  `audio_context/` (audio_service + audio_session background playback →
  setup), `output_device/` (device picker → setup), `metronome/` (→
  scheduling), `audio_data/` + `wave_data/` (→ visualization), `filters/` (→
  filters), `buffer_stream/` (web radio, WebSocket, PCM generation →
  streaming), `pull_buffer/` (HTTP range streaming → pull-streaming),
  `mixing_bus/` (→ mixing-bus), `mixer_capture/` incl. `isolate_capture_test`
  (→ output-capture), `waveform/` (→ synthesis).
- GitHub issues/PRs at `https://github.com/alnitak/flutter_soloud` — mined for
  recurring user mistakes to feed the "traps" sections.

## Deliverables

### 1. Installer: a single self-contained `bin/skills.dart`

One file only — `dart run flutter_soloud:skills` resolves `bin/skills.dart`
automatically; no `pubspec.yaml` change needed. All install logic lives in
this file (flutter_scene splits it into `lib/` only because its `init` and
`skills` commands share code; flutter_soloud has just the one command).

Must import only `dart:io` / `dart:isolate` so it runs under plain `dart run`
(importing `package:flutter_soloud/flutter_soloud.dart` would pull in Flutter
and break the CLI).

Flags: `--check` (report installed vs bundled versions, exit non-zero when an
install/update is available), `--help`, and a hidden `--project-root <dir>`
override so tests can target a temp directory.

Behaviour (adapted from flutter_scene's `init_command.dart`, skills-only —
no hook/pubspec mutation):
- Bundled skills live at package root `skills/<name>/SKILL.md` (+ optional
  `references/` per skill). Located via
  `Isolate.resolvePackageUri(package:flutter_soloud/flutter_soloud.dart)`
  then `../skills/`.
- Installs into every agent parent dir already present in the project
  (`.claude`, `.cursor`, `.codex`, `.opencode`, `.cline`, `.gemini`,
  `.github`, `.agents`), defaulting to `.agents` when none exists.
- Each `SKILL.md` frontmatter carries a `version:` int; the installer
  compares versions to decide install / update / up-to-date.
- Idempotent; overwrites on update.

### 2. Skills (under `skills/` at package root)

One directory per skill: `skills/<name>/SKILL.md`, with `references/*.md` only
where the content is too long for one file (playback, streaming).

Format per skill (following flutter_scene's idiom skill):
frontmatter (`name`, `version: 1`, `description` written as a trigger —
"Use when building X with flutter_soloud…"), then: minimal working example
that compiles as-is → API shape and where it diverges from what models assume
(audioplayers/just_audio habits) → traps that fail silently → pointers to
deeper references → "keeping this skill current" footer mentioning
`dart run flutter_soloud:skills --check`.

Proposed catalog (14 skills):

| Skill | Covers | Main sources |
|---|---|---|
| `flutter_soloud-idioms` | Core mental model: `SoLoud.instance` singleton, `AudioSource` vs `SoundHandle`, init-before-use, max 16 voices default, sync `play()` can't throw → `audioDeviceStartFailures`, not audioplayers/just_audio. Cross-cutting web/native gotchas. | soloud.dart, setup.mdx, issues |
| `flutter_soloud-setup` | pubspec add, platform setup (web `init_module.dart.js` script tag, Linux `libasound2-dev`, Android minSdk 21), `init()` options (sampleRate, bufferSize, channels, lowLatency, renderAheadFrames), `deinit()`, build hooks, `no_xiph_libs` shrink option, logging setup. **OS audio integration / background playback**: `audio_session` configuration + interruption/ducking/becoming-noisy handling, `audio_service` `AudioHandler` wrapper (media notification, lock-screen controls, seek bar via position timer), required iOS `Info.plist` (`UIBackgroundModes: audio`) and Android manifest/MainActivity changes — distilled from `example/lib/audio_context/audio_context.dart` into a `references/audio_context.md`. **Output devices**: `listPlaybackDevices()` (callable pre-init) + `changeDevice()` device-picker pattern from `example/lib/output_device/output_device.dart`, incl. the Android/iOS/Web default-device-only constraint | setup.mdx, no_xiph_libs.mdx, logging.mdx, web_notes.mdx, audio_context.mdx, example/lib/audio_context/audio_context.dart, example/lib/output_device/output_device.dart |
| `flutter_soloud-loading` | `loadFile`/`loadAsset`/`loadMem`/`loadUrl`, `LoadMode.memory` vs `disk`, formats (MP3/WAV/OGG/FLAC), `disposeSource`, `soundEvents`, `autoDispose`, `allInstancesFinished`, `joinTwoSources`, parallel loading, web = `loadMem` only | loading.mdx, soloud.dart |
| `flutter_soloud-playback` | `play()`/`playSource`, pause/resume/stop/seek, looping + loop points (half-open `[start,end)`), play speed, voice groups/protection, `getPosition`/`getLength` | playback.mdx |
| `flutter_soloud-scheduling` | Sample-accurate timing: `playClocked`, `playScheduled`/`stopScheduled`/`fadeScheduled`, `getEngineTime`/`getPlayheadTime`/`getOutputLatency`, metronome/sequencer patterns | playback.mdx, example metronome |
| `flutter_soloud-volume-pan` | Per-handle/global volume, pan/`setPanAbsolute`, fades & oscillators, `schedulePause`/`scheduleStop`, `getApproximateVolume`, limiter anti-clip tip | volume.mdx |
| `flutter_soloud-filters` | All 12 filters (echo, freeverb, biquad, bassboost, flanger, waveShaper, lofi, robotize, parametricEq, pitchShift, compressor, limiter), global vs per-sound vs per-bus scope, param fade/oscillate, web constraint (no per-sound filters), max 8 per sound | filters.mdx, lib/src/filters/* |
| `flutter_soloud-3d-audio` | `play3d`/`play3dClocked`/`play3dScheduled`, listener position/at/up/velocity, source min/max distance, attenuation models, Doppler, `set3dSoundSpeed` | 3d_audio.mdx |
| `flutter_soloud-streaming` | Push streams: `setBufferStream`, `BufferingType.preserved` vs `released`, PCM + compressed `auto` formats, `addAudioDataStream`, `onBuffering`/`onMetadata`, icy metadata, radio/WebSocket/PCM-generation recipes from `example/lib/buffer_stream/` (`web_radio.dart`, `websocket.dart`, `generate.dart`) | streaming.mdx |
| `flutter_soloud-pull-streaming` | Pull streams: `setPullBufferStream`, `onMoreDataIsNeeded`, `addPullBufferDataStream`, `getPullBufferTimeRange`, seek support, bounded-memory playback of huge sources; HTTP range-request streaming recipe from `example/lib/pull_buffer/` (`http_range_stream.dart`, `file_stream.dart`, `seek_bar.dart`) | pull_buffer_streaming.mdx |
| `flutter_soloud-mixing-bus` | `createMixingBus`, `playOnEngine`, routing via bus/`busId`, `annexSound`, bus filters/volume/channels, music/SFX/UI sub-mix pattern | mixing_bus.mdx |
| `flutter_soloud-visualization` | `setVisualizationEnabled` (window size, kind, channel), `audioVisualizationEvents`, `AudioVisualizationData`, `setFftSmoothing`, CustomPainter visualizer recipe, `readSamplesFromFile/Mem` for waveform UIs | audio_data.mdx |
| `flutter_soloud-synthesis` | `loadWaveform` (9 oscillator types, super-wave, detune), runtime waveform setters, `speechText` TTS, `SoLoudTools.createNotes` | waveforms.mdx |
| `flutter_soloud-output-capture` | Recording the mix: `startMixerOutputStream` (PCM + Opus/Vorbis/FLAC/WAV), `getMixerOutputWavHeader` header fix, `chunkPCMFrames`, save-to-file recipe, `SoLoudIsolate` for off-main-isolate capture | mixer_output_capture.mdx, soloud_isolate.dart |

(Device/OS integration — `audio_session`, `audio_service` background playback,
device switching, idle timeout — is folded into `flutter_soloud-setup` and
`-idioms` rather than its own skill.)

### 3. Tests & verification

- Test (plain `dart test`, e.g. `test/skills_install_test.dart`) driving the
  CLI as a subprocess with `--project-root <tempdir>`: install, re-run
  idempotency, `--check` exit codes, version-bump update detection,
  default-to-`.agents` behaviour.
- Manual verify: `dart run flutter_soloud:skills --check` and a real install
  from the `example/` project; confirm files land in the expected skill homes.
- `dart analyze` on the new files; confirm `bin/skills.dart` has no Flutter
  transitive imports (`dart run` smoke test outside Flutter context).

### 4. Docs touch-ups

- README (and docs repo, if wanted): one short section telling users to run
  `dart run flutter_soloud:skills` to install the agent skills.

## Execution order

1. Save this plan to `SKILLS.md` at the repo root (per request).
2. Write the installer (`bin/skills.dart`, self-contained).
3. Author skills in parallel (one subagent per skill, given its doc sources and
   the shared format template), starting with `flutter_soloud-idioms` as the
   template-setter. Mine GitHub issues/PRs for trap material.
4. Review every skill for API accuracy against `lib/src/soloud.dart` signatures.
5. Tests + verification as above.
6. README section.

## Notes / decisions

- Skill granularity: the catalog above is fine-grained (one skill per feature).
  Alternative: consolidate to ~5 broader skills (core, playback, effects,
  streaming, advanced) — fewer files, but longer individual skills and weaker
  trigger matching.
- No changes to `pubspec.yaml`, build hooks, or native code are required.
- Skills ship with the package automatically (pub publishes `skills/` since it
  isn't gitignored); each `SKILL.md` `version:` bump drives update detection.

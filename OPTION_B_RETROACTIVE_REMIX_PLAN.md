# Option B — Retroactive Re-mixing ("Rewrite-Ahead") Implementation Plan

Goal: allow newly started (or modified/stopped) voices to become audible
**inside the audio that has already been mixed but not yet played**, so that a
reactive `play()` / `playScheduled(now)` has near-device-period latency even
when the engine is configured with a large buffer (e.g. 8192 frames ≈ 186 ms
at 44.1 kHz).

All paths below are relative to the `flutter_soloud` project root. Line
references are from the code as of the planning date — treat them as anchors,
not guarantees.

---

## 1. Background: why the latency exists today

Current pipeline (non-web):

```
Dart play()/playScheduled()
  → FFI → Player::playScheduled            (src/player.cpp:2119)
    → Soloud::playScheduled                (src/soloud/src/core/soloud_core_basicops.cpp:222)
      = play(paused) + setDelaySamples + setPause(0)
        where delay counts down from mStreamTime
        = first sample of the NEXT buffer to be mixed

miniaudio device callback soloud_miniaudio_audiomixer
  (src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp:410)
  → soloud->mix(pOutput, frameCount)       (:486)
  → MixerOutput::instance().onAudioData()  (:487)
```

- The device period equals the engine buffer:
  `deviceConfig.periodSizeInFrames = aBuffer` (`soloud_miniaudio.cpp:740`).
- `mix()` writes **directly into the device-provided pointer**. Once the
  callback returns, those samples belong to miniaudio/CoreAudio and cannot be
  edited.
- Voice insertion is atomic w.r.t. the audio mutex, so a voice added between
  callbacks is first mixed into the *next* period, which is heard ~1 period
  after the one currently playing → 0–2 periods of insertion latency
  (~93–186 ms at 8192 frames), plus device latency.
- `playScheduled`'s sample-accurate mid-buffer starts do not help: they
  control placement *within* the buffer being mixed, not how soon that buffer
  reaches the DAC.

**Fundamental constraint:** in a pull-based output, "samples already handed
to the device" = unchangeable latency. The only way to start a voice inside
already-mixed audio is to **interpose an engine-owned ring buffer between the
mixer and the device**, and to **re-mix the not-yet-consumed tail** of that
ring when an event lands inside the rendered window.

---

## 2. Non-goals

- No change to the public scheduling semantics of `playClocked` /
  `playScheduled` / `stopScheduled` / `fadeScheduled` — they keep their exact
  timing contracts; only the *minimum achievable* latency changes.
- No support for rewinding non-rewindable sources (see §7, fallback matrix).
- No change to the Dart API of existing methods. New configuration is
  additive only.
- Web/AudioWorklet gets the feature last (or a degraded version); the
  spin-mutex/try-lock path (`soloud_miniaudio.cpp:473-479`) constrains what
  can block on the render thread.

---

## 3. High-level design

```
                        ┌────────────────────────────────────────────┐
                        │  Engine (audio thread, or caller thread    │
                        │  during retroactive re-mix)                │
                        │                                            │
  Dart events ─────────▶│  Soloud::mix(period) ──▶ RENDER-AHEAD RING │──┐
  (play/stop/fade/...)  │        ▲                 (float, interleaved│  │
                        │        │                  engine-owned)     │  │
                        │   rollback & re-mix      write head keeps   │  │
                        │   of unconsumed tail     ~renderAhead ahead │  │
                        └────────────────────────────────────────────┘  │
                                                                         ▼
  miniaudio callback (small device period, e.g. 256–512 frames)   read head
    → memcpy(ring, pOutput, frameCount)  → MixerOutput tap → device → DAC
```

Three pillars:

1. **Render-ahead ring (§4.1).** The engine mixes into its own ring instead
   of the device pointer. The device callback becomes a cheap `memcpy` (plus
   silence on underrun), so the **device period can be small** (256–512
   frames) regardless of the engine mix quantum (8192). Write head stays
   `renderAheadFrames` ahead of the device read head.

2. **Checkpoints (§4.3).** At every mix-period boundary, snapshot all mixer
   state needed to reproduce that period's mix exactly (engine POD state +
   per-voice POD state + resampler ping-pong contents + filter state where
   supported + source-consumption offsets).

3. **Retroactive re-mix (§4.5).** When an event's effective time falls inside
   the rendered-but-unplayed window `[readHead, writeHead)`: restore the
   checkpoint at or before the event time, apply the event (insert/modify/stop
   the voice), and re-mix forward to the write head, overwriting the ring.
   Events in the past relative to the read head fall back to today's
   as-soon-as-possible behavior (start at write head).

---

## 4. Detailed design

### 4.1 Render-ahead output ring

New component, e.g. `src/soloud/src/core/soloud_render_ring.{h,cpp}`.

- Interleaved float32 ring, `channels × ringFrames`. `ringFrames =
  renderAheadFrames + mixQuantum + small device period margin`. With
  `renderAheadFrames = 8192` and device period 512: ring ≈ 9–10 kframes.
- Two positions:
  - **write head** — next frame the mixer will produce. Corresponds to engine
    `mStreamTime` at the last completed mix.
  - **read head** — next frame the device callback will consume.
- Ownership: written by whoever calls `mix()` (audio thread normally; caller
  thread during re-mix — always under `mAudioThreadMutex`), read by the
  miniaudio callback. The callback keeps its current discipline: try-lock /
  silence-on-contention on web, plain path elsewhere. Reading can be made
  lock-free (SPSC with atomics, like `MixerOutput`'s own ring,
  `src/mixeroutput/mixer_output.h:191-197`) so the callback never blocks even
  while a re-mix is in progress — the re-mix only overwrites frames *ahead*
  of a published watermark; frames at/behind the read head are never touched.
- **Underrun policy:** if the callback finds fewer than `frameCount` frames
  available, emit silence for the gap (log once via `soloud_platform_log`).
  This mirrors today's behavior class but is now recoverable without
  device-level glitching.
- The miniaudio device config changes: `periodSizeInFrames` becomes the new
  small device period (256–512, configurable), **decoupled** from the engine
  `bufferSize`. `postinit_internal` is currently called with the *actual*
  device period (`soloud_miniaudio.cpp:798/842/880`) — with the ring, the
  engine buffer size must instead come from the *configured* engine quantum,
  not the device period. Adjust those call sites accordingly.

Where the mix is driven from: two options.

- **A. Callback-driven top-up (recommended).** The miniaudio callback, after
  copying out, checks if `writeHead - readHead < renderAheadFrames`; if so it
  calls `mix()` for one quantum (holding the mutex as today). Keeps a single
  producer, no extra thread, identical threading model to today. Mixing an
  8192 quantum from the device callback is exactly what happens today, so no
  new real-time-safety concerns.
- **B. Dedicated mixer thread.** Lower worst-case callback time, but adds a
  thread, a wakeup mechanism, and new real-time/locking surface. Rejected for
  v1.

### 4.2 Time model

- Keep `mStreamTime` semantics exactly as-is (advanced before mixing,
  `soloud.cpp:2172-2174` = time of the first sample of the next mixed
  quantum). The write head always equals `mStreamTime`.
- New: track the **play head time** `mPlayheadTime = mStreamTime -
  (writeHead - readHead)/samplerate`, updated as the device consumes. Needed
  to decide whether an event time is inside the rewritable window
  `[mPlayheadTime, mStreamTime)`.
- `getEngineTime()` keeps returning `mStreamTime` (the mix clock) so existing
  scheduling code is unaffected. Add `getPlayheadTime()` for diagnostics and
  for the Dart-side "true output latency" display.
- `getStreamPosition`/`getStreamTime` per-voice semantics are unchanged
  (they describe mix-side state).

### 4.3 Checkpoints (snapshot/restore)

At the **end of each mix quantum** (still under the audio mutex, before
unlock), capture a `MixCheckpoint`:

Engine-level (all POD, trivial):
- `mStreamTime` (double), `mGlobalVolume`, `mGlobalVolumeFader` (POD,
  `soloud_fader.h:33-62`), `mActiveVoice[]/mActiveVoiceCount/
  mActiveVoiceDirty`, `mHighestVoice`, play-index counter `mPlayIndex`,
  clocked-anchor state (`mClockedAnchorTime/Sample/LastTime`,
  `soloud.h:700-708`), visualization accumulators if enabled.

Per active voice (`AudioSourceInstance`, `soloud_audiosource.h:104-213`) —
all POD members, copied by value into a checkpoint-owned struct (not a
shallow struct copy, since `mResampleData`/`mFilter` are pointers):
- timing: `mStreamTime`, `mStreamPosition`, `mSourceSamplePosition`
- speed: `mSetRelativePlaySpeed`, `mOverallRelativePlaySpeed`, `mSamplerate`
- volume/pan: `mPan`, `mChannelVolume[]`, `mSetVolume`, `mOverallVolume`,
  `mCurrentChannelVolume[]` (click-ramp state!), `mActiveFader`
- faders: `mPanFader`, `mVolumeFader`, `mRelativePlaySpeedFader`,
  `mPauseScheduler`, `mStopScheduler` — all `Fader`, POD
- scheduling: `mDelaySamples`, `mStopSamplesLeft`
- looping: `mLoopCount`, `mLoopPoint`, `mLoopEndPoint`
- flags: `mFlags` (LOOPING/PAUSED/INAUDIBLE*/etc.)
- resampler: `mSrcOffset`, `mLeftoverSamples`, **contents of both ping-pong
  blocks** `mResampleData[0..1]` (512 × channels floats each, from the shared
  pool — the pool mapping `mResampleDataOwner` is restored to match)
- 3D: `m3dData[ch]` (`soloud.h:738`)
- handle ↔ voice-slot mapping implied by the voice list itself

**⚠ Fader hazard (must respect):** `Fader::get()` treats a time *before*
`mStartTime` as clock rollover and restarts the fade
(`soloud_fader.cpp:93-103`). Re-mixing therefore must **never** tick faders
against restored-past times without restoring the fader POD first. With full
POD restore this is automatic; document the invariant in the checkpoint code.

Not generically snapshot-able — needs a new virtual interface:
- **Per-voice filter instances** (`voice->mFilter[8]`) and **global filter
  instances** (`mFilterInstance[8]`): state is arbitrary per subclass (e.g.
  `LimiterInstance` delay ring `src/filters/limiter.h:35-49`,
  `PitchShiftInstance`'s `SignalsmithStretch`). Add to `FilterInstance`
  (`soloud_filter.h:34-53`):

  ```cpp
  /// Returns a heap snapshot of the instance's mutable state, or nullptr
  /// if this filter cannot be re-mixed (then retroactive events affecting
  /// a voice using it are applied going forward only).
  virtual FilterStateSnapshot *captureState() { return nullptr; }
  virtual void restoreState(FilterStateSnapshot *) {}
  ```

  Implement for the simple built-ins first (biquad, echo, lo-fi, flanger,
  bassboost, dc-removal, EQ — mostly small POD delay lines); leave
  `PitchShift`/`Limiter` as `nullptr` initially (documented degradation).
- **Source-consumption state** (subclass fields): add a parallel virtual pair
  on `AudioSourceInstance` (`captureSourceState` / `restoreSourceState`):
  - `BufferStreamInstance` PRESERVED: snapshot = `mOffset`
    (`audiobuffer.h:30`) — consumed data remains in the buffer
    (`buffer.h:139-153`), so re-reading is exact. ✅
  - Seekable decoded sources (Wav/MP3/OGG fully loaded into memory): seek to
    `mSourceSamplePosition` on restore. ✅ (verify decoder exactness per
    source — `dr_libs`/`minimp3` decoders are deterministic for a fresh seek)
  - `BufferStreamInstance` RELEASED: consumed data is destroyed
    (`buffer.h:112-120`). ❌ → fallback (§7)
  - Live streams / pull streams / speech synth: ❌ → fallback

Checkpoint storage: ring of the last `ceil(renderAheadFrames / quantum) + 1`
checkpoints, fixed-size pool (no allocation on the audio path after init).
With quantum 8192 and renderAhead 8192 that's 2–3 checkpoints; each is
`O(voices × ~200 bytes + resampler contents 2×512×ch×4B × voices)` ≈ a few
hundred KB at 128 voices — acceptable, but consider storing resampler
contents only (skip second ping-pong block if `mLeftoverSamples == 0`) to
halve it.

### 4.4 Retroactive trigger points

Classify every engine entry point (all already serialize on
`mAudioThreadMutex` via `FOR_ALL_VOICES_PRE/POST` or explicit locks):

| API | Effective time | Retroactive? |
|---|---|---|
| `play()` | now (write head today) | re-mix from current quantum start, inserting voice with delay = max(0, now − playhead) |
| `playScheduled(t)` | t | if `t < mStreamTime` and `t ≥ playhead` → re-mix from checkpoint at ⌊t⌋ quantum boundary with the voice's delay set to `t`; else current behavior |
| `playClocked(t)` | anchored time | same as `playScheduled` after anchor mapping |
| `stop`, `setPause`, `setVolume/Pan/RelativePlaySpeed` | immediate | if the voice is sounding inside the window → re-mix window with the change applied at the appropriate offset (sample-accurate within window: apply at `max(event time, playhead)`) |
| `stopScheduled(t)` / `fadeScheduled(..., t, ...)` | t | if inside window → re-mix; else current behavior |
| `seek()` | immediate | source-state restore handles it; treat like a modifying event |
| parameter faders (`fadeVolume`, etc.) | relative | re-anchor onto re-mixed timeline; document |

Immediate-effect setters get *sub-quantum* accuracy for free inside the
window — a genuine upgrade over today, where e.g. `stop` is quantized to the
next mix.

### 4.5 Re-mix procedure

Triggered from any retroactive event (caller thread, under
`mAudioThreadMutex`):

1. Compute event engine-time `t` (for `play()`: `t = now`, where
   `now = mStreamTime` at the last quantum start... precisely: the current
   write-head time).
2. If `t < playheadTime` → clamp to write head (today's ASAP behavior). Done.
3. Find checkpoint `C` = latest checkpoint with `C.time ≤ t`. (Checkpoints
   exist at quantum boundaries covering the whole window.)
4. Publish a ring **rewrite watermark** = read head (frames behind it are
   frozen); compute the frame range `[frame(C.time), writeHead)` to rewrite.
5. Restore `C` (engine + voices + filters + source states). For a **new**
   voice, create its instance now (`Soloud::play` already creates instances
   outside the mutex, `soloud_core_basicops.cpp:40-45` — keep that), insert
   it with `mDelaySamples = (t − C.time) × samplerate`.
6. Re-mix forward quantum-by-quantum from `C.time` to the old write head,
   writing into the ring (not the device). Reuse `mix_internal` as-is — it is
   already parameterized on buffer/stride; the ring write pointer plays the
   role of `pOutput`. Do **not** re-run the `MixerOutput` tap or
   visualization for the rewritten region (see §4.6).
7. Advance `mStreamTime`/voice times back to the pre-rollback values (the
   re-mix naturally reproduces them — assert equality in debug builds).
8. Release the watermark. The device callback continues reading; the first
   rewritten frame it hits contains the new voice.

Cost per retroactive event: one extra partial re-mix of ≤ `renderAheadFrames`
worst case, ~8192 frames ≈ 1–3 ms of CPU for typical voice counts — fine for
piano/keyboard rates, and it happens on the caller thread off the device
callback. For rapid-fire events (chords, glissandi), **coalesce**: if several
events arrive before the re-mix runs, apply them all in one rollback (deferred
re-mix flag + drain on next lock acquisition).

### 4.6 Post-mix consumers

- **`MixerOutput::onAudioData`** (`soloud_miniaudio.cpp:487`,
  `mixer_output.cpp:256`): move the tap from "after mix" to **after ring
  read** (device callback, on the consumed frames). Then capture/PCM-output
  always reflects what was actually played, including rewrites — otherwise
  capture would contain the pre-rewrite audio. This also means capture gets
  the small device period's chunking; verify `MixerOutput`'s ring sizing and
  Dart-side `MixerOutputStreamManager` cope (they take arbitrary frame counts
  already).
- **Visualization / FFT / wave** (`soloud.cpp:2293-2332`, `src/analyzer.cpp`):
  computed inside `mix_internal` from `mScratch`. During re-mix these would be
  recomputed for the rewritten window and momentarily show it twice.
  Options: (a) accept — it's a 100-ms-scale transient in a debug feature; (b)
  gate visualization updates during re-mix passes with a flag. Pick (b) —
  it's a one-line guard.
- **Ended-voice dispatch**: `unlockAudioMutexAndDispatchEndedVoices_internal`
  releases the mutex mid-unlock (`soloud.cpp:2438-2504`). A re-mix must never
  be in its "restore" phase across that window: do the whole re-mix inside a
  fresh, complete lock hold, never piggybacked on the unlock path. If a voice
  ends *inside the rewritten window* (its `hasEnded()` now triggers earlier
  in wall-clock terms), its ended-callback fires at re-mix time, i.e. up to
  `renderAhead` earlier than today — **document this**, and check Dart-side
  autoDispose logic doesn't mind earlier callbacks (it shouldn't; it already
  tolerates scheduling jitter).

### 4.7 Threading summary

- All checkpoint/re-mix work happens under `mAudioThreadMutex`. The device
  callback keeps today's locking (blocking on native, try-lock+silence on
  web, `soloud_miniaudio.cpp:473-484`) **for the top-up mix**, but the ring
  *read* itself is lock-free SPSC so a long re-mix on the caller thread can
  never glitch the device — worst case the callback reads pre-rewrite audio
  for one period (acceptable, bounded).
- Emscripten: the re-mix runs on the main thread holding the spin mutex; the
  worklet emits silence on contention (existing fallback). Acceptable for v1;
  consider capping `renderAheadFrames` on web.

### 4.8 Buses

`BusInstance::getAudio` recursively calls `mixBus_internal`
(`soloud_bus.cpp:43-57`) — buses are voices, so their instance state is
covered by the per-voice snapshot (bus instances carry the same
`AudioSourceInstance` base state; verify `BusInstance` adds no extra mutable
state beyond the base + its own fader — check `soloud_bus.h` during
implementation). Scheduled/retroactive events targeting a bus
(`bus.playScheduled`) go through the same path.

---

## 5. Configuration & API plumbing

New init-time config (all optional, defaults = current behavior, i.e.
feature **off** unless requested):

```
SoLoud.init({
  ...
  int bufferSize = 2048,            // engine mix quantum (unchanged meaning)
  int? devicePeriodFrames,          // NEW: small output period, e.g. 512
  int? renderAheadFrames,           // NEW: rewrite window, e.g. 8192; null/0 = feature off
})
```

Plumbing path (every hop must be updated — note `initPlayer` is invoked
inside `Isolate.run` with a fixed 5-int signature,
`lib/src/bindings/bindings_player_ffi.dart:70-92`):

1. `lib/src/soloud.dart:401-430` — `init()` params + docs.
2. `lib/src/bindings/bindings_player.dart:185` — abstract signature.
3. `lib/src/bindings/bindings_player_ffi.dart` — ffigen signature + isolate
   worker invocation.
4. `lib/src/bindings/bindings_player_web.dart:386` — web variant (accept and
   cap/ignore for v1).
5. `src/bindings.cpp:909-960` — `initEngine` export.
6. `src/player.cpp:284` — `Player::init` → `soloud.init(...)`.
7. `src/soloud/src/core/soloud.cpp` / `soloud.h:329` — `Soloud::init` stores
   config; `postinit_internal` allocates ring + checkpoint pool.
8. `src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp:729` —
   `miniaudio_init` uses `devicePeriodFrames` for
   `deviceConfig.periodSizeInFrames` (:740); also update the device-reinit
   path (:1002-1013).

New read-only API (both Dart + FFI): `getPlayheadTime()`,
`getOutputLatency()` (= renderAhead + device cushion, measured),
`isRetroactiveRemixEnabled`.

---

## 6. Interaction with existing features (checklist)

- [ ] `playClocked` anchor logic (`soloud_core_basicops.cpp:121-172`): its
  built-in 2×`mBufferSize` lead is premised on the old latency model. With
  rewrite-ahead, recompute the lead from `renderAhead + device cushion`
  instead of `2 × mBufferSize`, or leave as-is (conservative) for v1.
- [ ] `scheduleStopAt` / `mStopSamplesLeft` (`soloud_core_faderops.cpp:72`,
  countdown at `soloud.cpp:1691-1707`): sample-accurate stops inside the
  window become editable — verify restore of `mStopSamplesLeft` per voice.
- [ ] Voice stealing (`findFreeVoice_internal`,
  `soloud_core_getters.cpp:323-360`) during a retroactive `play` that rolls
  back: the steal decision must be re-evaluated against the *restored* voice
  set. Add a test with voice count at max.
- [ ] `mEndedVoiceQueue` entries for voices that ended in the *original* mix
  of the rewritten window but not in the re-mix (or vice versa): reconcile —
  an ended-callback must fire exactly once per voice end. Simplest correct
  rule: queue dispatch happens only for endings at/behind the read head;
  endings inside the rewritten window are re-evaluated at re-mix.
- [ ] `BufferStream` buffering pauses (`checkBuffering`,
  `audiobuffer.cpp:469-530`): a PRESERVED stream voice restored to an earlier
  `mOffset` must re-run its buffering bookkeeping consistently.
- [ ] 3D audio: `update3dAudio` runs outside the audio mutex on `m3dData`
  copies — retroactive 3D parameter changes are out of scope for v1; document.
- [ ] `MixerOutput` session/admission gates (`CapturePass`) — unaffected, but
  the tap moves (§4.6).
- [ ] Analyzer window sizing derived from `getBackendBufferSize()`
  (`bindings.cpp:938-941`): with device period decoupled, make sure this uses
  the engine quantum, not the small device period.

---

## 7. Fallback matrix (degrade gracefully, never fail)

| Source / filter involved in the event | Retroactive behavior |
|---|---|
| Fully-loaded wav/ogg/mp3/flac (seekable) | Full re-mix |
| BufferStream PRESERVED | Full re-mix (snapshot `mOffset`) |
| BufferStream RELEASED | No rollback for that voice; event applies going forward (today's behavior) |
| Live pull streams, speech, noise/synth with non-restorable state | Same — going forward only |
| Voice with a non-snapshot-able filter (pitch shift, limiter v1) | Re-mix allowed only if the voice isn't audible in the rewritten region; else going forward |
| Event time behind play head | ASAP at write head (today) |
| Feature disabled (default) | Identical to today |

Design rule: per-voice `canRetroactivelyRemix()` computed from the above; the
re-mix skips/restores only eligible voices, and events on ineligible voices
take the legacy path. This keeps the feature safe to enable globally.

---

## 8. Implementation phases

**Phase 1 — Ring + decoupled device period (no re-mix yet).**
Render-ahead ring, small device period, tap moved to ring read,
`getPlayheadTime()`. Behaviorally identical to today except output latency is
now `renderAhead + device cushion` and is *configurable*. Ship-worthy on its
own (it subsumes "Option A"). Tests: existing suite + latency measurement
harness.

**Phase 2 — Checkpoints.**
POD snapshots (engine + voices + resampler contents), capture at quantum
boundaries, debug-only round-trip verification (mix twice from a checkpoint,
assert bit-identical output — this test alone validates the snapshot set).

**Phase 3 — Retroactive play/stop/setters.**
Re-mix procedure (§4.5) for `play`, `playScheduled` in-window, `stop`,
`setPause`, volume/pan/speed setters. PRESERVED BufferStream + seekable
sources only. Filter snapshot interface with built-ins stubbed to "simple
POD" where trivial.

**Phase 4 — Coverage & polish.**
`stopScheduled`/`fadeScheduled` in-window, event coalescing, filter snapshot
coverage expansion, web build support, ended-voice reconciliation audit,
documentation + metronome/piano examples updated.

---

## 9. Testing plan

- **Bit-exactness test (the cornerstone):** render N quanta normally; render
  again with an empty retroactive event (rollback + re-mix with no changes);
  assert bit-identical ring content. Catches every missing snapshot field.
- **Insertion accuracy test:** known click source, retroactive `play` at time
  t inside the window; assert first non-zero sample lands at the exact frame.
- **Latency harness:** instrumented run measuring keypress-to-ring-read-head
  and keypress-to-device; target ≤ device period + ~5 ms at any bufferSize.
- **Soak:** random retroactive events vs. non-retroactive reference mix where
  events were pre-scheduled; compare final mixes sample-wise (within fader
  float tolerance).
- **Stress:** max voices + stealing during rollback; RELEASED-stream fallback;
  underrun recovery; Emscripten contention path.
- Existing test suite must pass unchanged with the feature **off**, and with
  it **on** (defaults keep it off).

---

## 10. Risks / open questions

1. **Snapshot completeness** is the whole game — any missed mutable field
   silently corrupts the re-mixed audio. The bit-exactness test (§9) is
   mandatory from Phase 2, not optional.
2. **Decoder determinism on seek** for mp3/ogg (minimp3/dr_libs): verify a
   seek-to-frame + decode reproduces the original samples exactly; if not,
   restrict full re-mix to PCM/preserved sources.
3. **Ended-voice callback timing** shifts earlier by up to `renderAhead` —
   audit Dart autoDispose / UI listeners.
4. **CPU spikes**: a rollback during an already-heavy quantum doubles mix cost
   for that window. Bounded by coalescing + a "max re-mixes per second" clamp
   (beyond it, events degrade to going-forward).
5. **Web**: spin-mutex + no blocking on the worklet thread — v1 may ship
   native-only with a documented capability query.
6. Open: should `renderAheadFrames` be allowed to change at runtime
   (ring realloc)? Recommend init-time only for v1.

---

## 11. Rough effort estimate

- Phase 1: 2–4 days (well-bounded, mostly backend + plumbing).
- Phase 2: 3–5 days (snapshot plumbing + bit-exactness harness).
- Phase 3: 5–8 days (re-mix core + per-source restore + event matrix).
- Phase 4: 3–5 days (coverage, web, docs, soak).

Total ≈ 2.5–4.5 weeks for one engineer familiar with the codebase, with
Phase 1 independently shippable.

---

## 12. Implementation status (as of this session)

**Implemented and tested** (all native harnesses green; see
`test/render_ring_test.cpp`, `test/checkpoint_test.cpp`,
`test/retroactive_remix_test.cpp`, `test/render_ring_device_test.cpp`):

- **Phase 1** — render-ahead ring (`src/soloud/include/soloud_render_ring.h`),
  decoupled device period (`devicePeriodFrames`/`renderAheadFrames` on
  `SoLoud.init()`, plumbed Dart → FFI → `initEngine` → `Player::init` →
  `Soloud::setRenderAheadConfig`), `MixerOutput` tap moved after the ring
  read, `getPlayheadTime()` / `getOutputLatency()` / `isRenderAheadEnabled`.
- **Phase 2** — checkpoints (`soloud_checkpoint.{h,cpp}`): full engine/voice/
  resampler POD snapshots at quantum boundaries, filter/source snapshot
  virtuals, bit-exactness harness (rollback + re-mix with no event is
  bit-identical; mutation-tested).
- **Phase 3** — retroactive `play`/`playScheduled` (in-window),
  `stop`/`scheduleStopAt`, `setPause` (pause only), `setVolume`/`setPan`/
  `setRelativePlaySpeed` and the matching fades, with an in-window event
  journal (births + deaths + param changes) so back-to-back retroactive
  events (chords) compose. Source snapshots implemented for `Wav` and
  PRESERVED `BufferStream`. Ended-callback exactly-once reconciliation.
- **Phase 4 (partial)** — in-window `scheduleStopAt`/`scheduleFadeAt`,
  real-device smoke test.

**Design notes / deviations discovered during implementation:**

- Checkpoints are **re-captured during the re-mix** (with the ring position
  derived from the mix clock, not the ring heads, which do not move during a
  re-mix). Without this, a second rollback loses voices the first retroactive
  event inserted (the checkpoint chain would predate them).
  `findCheckpointAtOrBefore` prefers the newer serial on equal times.
- Journal replay is `>=` the checkpoint time, not `>`: an event landing
  exactly on a boundary postdates the capture and must be replayed.
- A rollback whose window contains a **voice death degrades to legacy
  behavior**: a deleted voice instance cannot be resurrected, so its audio
  would silently vanish from the rewritten window. A zombie-pool
  (deferred-delete) scheme could lift this; left as future work.
- Immediate setters/pause apply via zero-duration scheduled faders: quantum
  accuracy inside the window (the fader pass ticks once per quantum), which
  can land up to one quantum *early*; retroactive stop is sample-accurate.
- Retroactive unpause is not supported (legacy path); neither are retroactive
  3D changes (as planned).

**Not implemented (future work):** filter-state snapshots for the built-ins
(any voice or global filter that cannot snapshot disables rollback while
audible in the window — graceful degradation per §7), event coalescing for
rapid-fire streams (each event currently re-mixes independently; CPU is
bounded by the window size), the zombie-pool resurrection noted above, web
backend support (ring ignored on `__EMSCRIPTEN__`), runtime ring resize.

# Known bugs

Bugs found by the test suite that are not fixed yet. Each bug has an ID, and
that ID appears in the test that covers it. Run `grep -rn "<ID>" test` to
find the cases to re-enable once a bug is fixed.

How each kind of test marks a known bug:

- **Dart**: the case is marked `skip: 'bug <ID>: ...'`. Run it anyway with
  `flutter test --run-skipped <file>`.
- **Native**: a failing check prints `KNOWN BUG <ID> (...)` and does not fail
  the run. Pass `--strict` (or set `OSDEC_STRICT=1` for the OS decoder tests)
  to make these checks fail.

**When a bug is fixed:**

1. Remove the marker: delete the `skip:`, or turn the `KNOWN_BUG` check into
   a normal `CHECK`.
2. Check that the test passes, also with `--asan` where it applies.
3. Delete the bug's entry from this file.

Line numbers are approximate. IDs already used and retired: E1–E9, F1–F5,
O1–O3, S1–S9, K1–K2, P1–P9.

## Native parsers, decoders and files

| ID | Bug | Where | Test |
|---|---|---|---|
| A1 | stripIcyMetadata (title-only) calls back with unchanged title when only StreamUrl changes -> duplicate onTrackChange (FLAC, Ogg FLAC) | icy_metadata.h:92-95 | icy_metadata testUrl |
| A2 | Chained Ogg: seek index stores per-link granules, decoder looks up absolute granule -> seek past link 1 wrong (Vorbis same code untested) | ogg_seek_index.h:37-40, opus_stream_decoder.cpp:558-573 | ogg_seek_index testDecoderChained |
| A10 | Opus 3-8 ch decoded by single-stream decoder clamped to 2ch -> garbage | opus_stream_decoder.cpp:82,416,452 | opus testChannels |
| A11 | Chained Opus links with different channel counts: one decode() mixes both under last link's count | opus_stream_decoder.cpp:270-292,314 | opus testChained |
| A12 | Opus 5 ms end fade applied to capped mid-stream decode() | opus_stream_decoder.cpp:317-333 | opus testBounded |
| A13 | Opus position wrong after seek (frames vs interleaved samples; resumes up to a page early) | opus_stream_decoder.cpp:483,527; pull_buffer_stream.cpp:1178 | opus testSeek |
| A14 | WAV seek skips target twice when buffered | wav_stream_decoder.cpp:151-158 | wav testSeek |
| A15 | Vorbis trims chained file with last link's EOS granule | vorbis_stream_decoder.cpp:242-247 | vorbis testChained |
| A16 | Vorbis stereo->mono link change: samples labelled mono then halved | vorbis_stream_decoder.cpp:282-285 | vorbis testChained |
| A17 | Vorbis misplaced end fade (as A12) | vorbis_stream_decoder.cpp:259-279 | vorbis testBounded |
| A18 | WAV input split inside a sample frame loses bytes; 1-byte pieces decode nothing | wav_stream_decoder.cpp:7-20,196 | wav testPieces |
| A19 | WAV data size 0xFFFFFFFF stops after first piece (dr_wav sizes from bytes at init) | wav_stream_decoder.cpp:124 | wav testDataSize |
| A20 | Pull stream seeked to/past end never ends | pull_buffer_stream.cpp:264,1262 | testSeekPastEnd |
| A21 | Out-of-buffer PCM seek byte offset not frame-aligned (channels swapped) | pull_buffer_stream.cpp:1638 | testSeekFrameAlignment |
| A22 | In-buffer seek between two frames swaps channels | pull_buffer_stream.cpp:267 | testSeekFrameAlignment |
| A23 | MemoryFile::read bounds check wraps -> memcpy ~4GB (heap overflow) | soloud_file.cpp:161 | soloud_file testMemoryFile |
| A24 | Failed openToMem frees data but keeps old length -> later read from address 0 | soloud_file.cpp:256-262 | testToMem |
| A25 | openFileToMem/openToMem ignore short read -> uninitialised tail | soloud_file.cpp:268,286 | testToMem |
| A26 | Wav rejects FLAC with total-samples 0 (spec: unknown) | soloud_wav.cpp:372 | soloud_wav_load testFlac |
| A27 | Truncated FLAC zero-padded to header length | soloud_wav.cpp:382 | testTruncated |
| A28 | After failed Wav reload a voice never ends (sample count not reset) | soloud_wav.cpp:462-466 | testInstanceAndReload |
| A30 | M4A bitrate read at wrong esds offset | m4a_metadata.cpp:183 | m4a testEsdsBitrate |
| A33 | ID3 COMM comment not converted to UTF-8 | aac_stream_decoder.cpp:508-520, mp3_stream_decoder.cpp:148-160 | metadata_parsers_fuzz testValid |
| A34 | Native FLAC NULL deref when frames have fewer channels than STREAMINFO | flac_stream_decoder.cpp:436 (crash :464) | metadata_parsers_fuzz testFlacChannels |

## Native concurrency, bindings and out-of-memory

| ID | Bug | Where | Test |
|---|---|---|---|
| B1 | NaN handling differs by build/position: clipRoundoff vector vs scalar tail, clipHard scalar only, peakAbs NEON vs scalar | simd_pcm.h:766,713,691; soloud_simd.h:403/418 | simd_pcm_test nanRound/nanHard/nanPeak |
| B2 | Data race on ActiveSound::handle: applyPauseState/play read without sounds_mutex while audio thread erases | player.cpp:1819,2688,2704-2706 vs 3131 | player_concurrency_test --tsan |
| B10 | Required pointer args dereferenced without null check (crash) | bindings.cpp many; player.cpp:1711; audiobuffer.cpp:583; pull_buffer_stream.cpp:809 | bindings_null_args_test |
| B11 | loadMem negative length -> SIGBUS | player.cpp:918,928 | bindings_null_args_test |
| B12 | fade*/oscillate*/schedulePause/scheduleStop return noError for handle 0 / made-up | bindings.cpp:3726+ | bindings_null_args_test |
| B13 | NaN/Inf floats accepted, reach float->int (UB) | bindings.cpp:3468,3489; soloud_core_basicops.cpp:359,477; soloud_wav.cpp:116; waveform.cpp:51; soloud.cpp:2793 | bindings_null_args_test |
| B14 | Filter param get/set on inactive filter / out-of-range attr return noError | filters.cpp:355,381; soloud_filter.cpp:165 | bindings_null_args_test |
| B15 | Enum/format ints unchecked; buffer streams accept 0/1000 channels; 0 ch div by zero | bindings.cpp:970,2586,3870; player.cpp:1223,1276,1407,4359; audiobuffer.cpp:402,417; analyzer.cpp:308 | bindings_null_args_test |
| B16 | setRealtimeStream no capacity limit (0xFFFFFFFF frames -> tens of GB) | realtime_ring_buffer.h init via realtime_stream.cpp:404 | bindings_null_args_test (flaky) |
| B17 | createBus() succeeds before init / after dispose | bindings.cpp:5051 | bindings_null_args_test |
| B18 | startMixerCapture negative bufferSizeBytes -> uncaught bad_alloc | bindings.cpp:971 | bindings_null_args_test |
| B19 | set3dSource* with made-up handle reads m3dData OOB before validating | soloud_internal.h:153-154 via bindings.cpp:4941 | bindings_null_args_test (UBSan) |
| B20 | filter-instance alloc failure in play() leaves audio mutex locked -> deadlock | soloud_core_basicops.cpp:102 (mutex :123) | out_of_memory_test voices_filter |
| B21 | bad_alloc escapes loadMem/loadWaveform exports -> abort via FFI | bindings.cpp:2516,2794; player.cpp:900 | out_of_memory_test bindings |
| B22 | same for play/play3d/createBus/busPlayOnEngine | bindings.cpp:2990,5051,5083 | out_of_memory_test |
| B23 | same for setBufferStream/addAudioDataStream | bindings.cpp:2586,2751 | out_of_memory_test |
| B24 | Wav::loadflac leaks dr_flac decoder when sample alloc fails | soloud_wav.cpp:363-379 | out_of_memory_test wav |
| B25 | WavStream::loadMem / WavStreamInstance ctor leak MemoryFile on alloc failure | soloud_wavstream.cpp:691-699,131,162,202 | out_of_memory_test wavstream |
| B26 | Player::loadWaveform leaves half-built entry on alloc failure | player.cpp:1739-1747 | out_of_memory_test bindings |

## Native DSP and engine

| ID | Bug | Where | Test |
|---|---|---|---|
| C1 | `Queue::play()` after the queue's voice ended returns success but is never heard: `findQueueHandle()` keeps the stale handle. | `soloud_queue.cpp:106,122-125` | `common/soloud_queue_test` (drain_restart) |
| C2 | A source destroyed while waiting in a Queue is still played: its instance reads the freed parent (use-after-free with Wav). | `soloud_queue.cpp:45`, `soloud_audiosource.cpp:228` | `common/soloud_queue_test` (destroyed) |
| C3 | `QueueInstance::getAudio` spins while a source returns 0 samples without ending (mixer hangs holding the audio mutex). | `soloud_queue.cpp:43-47` | `common/soloud_queue_test` (starving) |
| C4 | `FFTFilterInstance` passes 256 real samples to a complex FFT of 128 points: EQ not transparent; Bassboost boosts 22 kHz and images 172 Hz to 21.9 kHz (reachable from plugin). | `soloud_fftfilter.cpp:135`, `soloud_eqfilter.cpp:114`, `soloud_bassboostfilter.cpp:44` | `common/dc_removal_eq_filter_test` (eq_transparent, bassboost) |
| C5 | Vorbis encoder passes NaN to libvorbis (WAV/FLAC map NaN to 0): huge peaks. NaN passes NEON/scalar clip on ARM. | `vorbis_output_encoder.cpp:252-268` | `common/output_encoders_fuzz_test` (nan_in_tone) |
| C6 | `getStreamPosition()` keeps advancing while a voice is virtual; on return audio resumes where it stopped but position is ahead. | `soloud.cpp:3429` | `common/voice_virtualization_test` (resume_point) |
| C7 | `setProtectVoice()` does not set `mActiveVoiceDirty`: protecting a playing virtual voice has no effect until something else re-evaluates. | `soloud_core_setters.cpp:164-178` | `common/voice_virtualization_test` (protected) |

## Dart API (part 1)

| ID | Bug | Where | Test |
|---|---|---|---|
| H1 | MT web build: custom audio source (and likely custom filter) looks up the JS DSP function on the AudioWorklet thread's globalThis, but the documented setup installs it on the page's; the source is silent. | `src/synth/custom_source.cpp:124` (same lookup `src/filters/custom_filter.cpp:106`) | `common/custom_audio_source_test` (5 audibility tests, --wasm only) |
| H2 | `resetBufferStream` turns on ICY stripping (default metaint 16000) for streams that never asked for it: a plain MP3 stream loses a byte every 16000 and drops frames. | `src/audiobuffer/audiobuffer.cpp:434,469` | `common/buffer_stream_icy_test` ("a plain MP3 stream decodes fully after resetBufferStream") |

## Dart API (part 2)

| ID | Bug | Where | Test |
|---|---|---|---|
| J1 | FlacInfo.toString labels bitsPerSample as "Bitrate" | lib/src/metadata.dart:374 | common/metadata_format_test |
| J2 | AudioMetadata.toString writes "null" for MP3 with no mp3Metadata | metadata.dart:809 | metadata_format_test |
| J3 | setBufferIcyMetaInt / setMp3BufferIcyMetaInt documented to throw not-initialized but no init check | soloud.dart:1913,1926 | api_before_init_test |
| J4 | 3D setters/getters, destroyVoiceGroup, addVoicesToGroup, isVoiceGroup(Empty), createMixingBus have no init check | soloud.dart:4169-4210,4905-5069,5224 | api_before_init_test |
| J5 | At voice limit play() stops the oldest voice of same sound even if protected | player.cpp:2702-2706 (play3d 2832,2968) | max_voices_test |
| J6 | deinit() while loadMem running fails completers before listened -> uncaught async errors | soloud.dart:1129, 1506-1531 | async_multi_load_test |
| J7 | WAV capture header survives deinit/init | mixer_output.cpp:572,699 | mixer_output_wav_header_test |
| J8 | setVolume/setGlobalVolume/setRelativePlaySpeed/fades/play(volume) accept NaN/Inf | soloud.dart:3731,3774,3276,4218-4280; player.cpp:2607,3524,3534 | invalid_arguments_test |
| J9 | setFftDecibelRange stores NaN bounds | soloud.dart:4143 | invalid_arguments_test |
| J10 | 3D NaN/Inf positions/velocities/sound speed -> NaN in mix | soloud.dart:4905-5069 | invalid_arguments_test |
| J11 | FilterParam.value lets NaN through; fade to NaN -> NaN in mix | lib/src/filters/filters.dart:491,528 | invalid_arguments_test |
| J12 | startMixerOutputStream(bufferSizeBytes<0) aborts with length_error | bindings.cpp:864 -> mixer_output.cpp:498 | invalid_arguments_test |
| J13 | loadWaveform NaN/Inf scale/detune -> NaN samples | soloud.dart:2386, player.cpp:1717 | invalid_arguments_test |
| J14 | 3D min=max distance=0 with inverse attenuation -> 0/0 NaN | soloud_core_3d.cpp:165 | invalid_arguments_test |
| J15 | negative setPullBufferStream bufferSizeBytes / setRealtimeStream sampleRate -> huge alloc (WASM abort) | pull_buffer_stream.cpp:489, realtime_stream.cpp:351 | invalid_arguments_test (web) |
| J16 | Web loadMem of non-audio bytes returns a source (= L5) | soloud.dart:166-215 | async_multi_load_test |
| J17 | Web PCM pull stream onAudioDuration lost (callback registered after WASM call) | bindings_player_web.dart:904-985, pull_buffer_stream.cpp:581 | pull_buffer_stream_dart_test |
| J18 | Web addPullBufferDataStream always noError | bindings_player_web.dart:999-1031 | pull_buffer_stream_dart_test |

## Android, iOS, macOS and web

| ID | Bug | Where | Test |
|---|---|---|---|
| L1 | Android full decode keeps AAC end padding for edit-list M4As (trims to extractor duration, ignores edit list) | os_decoder_android.cpp:472-481 | android/native_stream_decoder_android_test (API 34 emulator) |
| L3 | Android streaming of AC-3 in MP4 wrong on device with AC-3 decoder: reads 93055/96000 frames, PCM after seek mismatches | native_stream_decoder_android.cpp read()/seek() (root cause unknown) | android/native_stream_decoder_android_test (S22) |
| L4 | Long metadata strings cut mid UTF-8 char (strncpy 1023) | audiobuffer.cpp:994, pull_buffer_stream.cpp:1680 | common/web_realtime_stream_test |
| L5 | Web loadMem/loadUrl accept undecodable bytes (silent source); native throws | lib/src/soloud.dart:167 (_loadMemWeb) | common/web_loader_test, web_decoders_test |

## Linux and FFmpeg

| ID | Bug | Where | Test |
|---|---|---|---|
| M1 | Failed Linux backend switch (JACK) leaves engine with no device: state uninitialized, play() noError, no fallback | soloud_miniaudio.cpp:2304-2311 | linux/linux_backend_switch_test |
| M2 | PulseAudio server loss: state stays started, clock freezes; after restart startAudioDevice() no-op, only changeDevice recovers | soloud_miniaudio.cpp/Player (miniaudio.h off-limits) | linux/pulse_server_loss_test |
| M4 | Without FFmpeg, AAC/M4A/AC-3 via loadMem/loadFile return generic fileLoadFailed (stream path audioFormatNotSupported) | soloud_wav.cpp:433 | linux/no_ffmpeg_test |
| M5 | Linux VST3 editor WM_DELETE_WINDOW ignored: ClientMessage data.l[0] read from eventBuf[8], LP64 index 7 | vst3_editor_linux.cpp:857 | linux/vst3_editor_linux_test |
| M6 | FFmpeg 5.x (lavf 59) crash: read with lavf 60+ AVStream layout | os_decoder_linux.cpp:206 (>=59 -> >=60; lavf59 codecpar@208), :351 (lavf59 time_base@16, duration@32) | docker ffmpeg 5 |
| M7 | FFmpeg 4.4 (lavc 58) heap over-read: AVCodecParameters read at FFmpeg7 offsets 132/152 past 140-byte struct | os_decoder_linux.cpp:398-405 | docker ffmpeg 4 --asan |
| M8 | FFmpeg 4.4: truncated M4A decodes longer (no AVStream duration fallback for lavf 58) | os_decoder_linux.cpp:351 | docker ffmpeg 4 |

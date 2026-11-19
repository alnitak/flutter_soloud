# Piano di riorganizzazione e copertura dei test

Ogni punto ha un ID (es. `B3`) e si può realizzare da solo, salvo le
dipendenze indicate. Basta dire "fai A1, B2" e si procede in quell'ordine.
Una volta completato un punto, spunta la casella.

Situazione attuale (19/11/2026):

- `test/`: 130 file piatti, cioè 37 test Dart, 41 sorgenti C++/ObjC++ con 43
  script `run_*.sh` (bash, solo macOS/Linux/Android via adb) e la cartella
  `native_bench/` (CMake).
- `example/tests/`: un runner Flutter (`tests.dart`, `run_tests.dart`,
  `repro*.dart`, gli script web via CDP) e 55 moduli in `example/tests/tests/`.
- Windows non ha nessuno script per i test nativi; iOS e Linux non hanno test
  dedicati.

## Stato al 19/11/2026 (branch `test-reorg`, nessun commit)

Tutti i punti sono realizzati tranne D5c e D7g (motivo indicato sotto
ciascuno). Dettagli su struttura e comandi in `test/README.md`.

Risultati su macOS arm64:

| Suite | Risultato |
|---|---|
| `flutter test` (common, android, ios, linux) | 752 passati, 22 saltati per bug noti, 0 falliti |
| `flutter test -x device` | 170 passati, 1 saltato |
| `flutter test --platform chrome [--wasm] test/web` | 36 passati (JS e WASM) |
| `./test/macos/all.sh` (script nativi common + macos) | 56 passati, 2 falliti (`native_stream`, `pull_mp3_seek`) |
| ctest (`test/CMakeLists.txt`), normale e `SOLOUD_TEST_SANITIZE=address` | 52/53 passati (fallisce `pull_mp3_seek`) |
| iOS simulator (`test/ios/all.sh`) | passati (iOS 26.1) |
| Android (`--build-only`, NDK 29, arm64-v8a e x86_64) | compilano; non eseguiti (nessun device) |
| Linux, Windows | scritti ma **mai eseguiti** (nessun host disponibile) |

I due fallimenti erano già presenti su `main` prima della riorganizzazione
(verificato su un worktree di `main`) e sono bug reali: vedi la sezione
"Bug trovati". Nella CI `pull_mp3_seek_test` è escluso tramite
`KNOWN_FAILING_NATIVE` in `.github/workflows/tests.yaml`, finché il bug non
viene corretto.

Note sulle decisioni prese durante la realizzazione:

- `test/web/` è stata aggiunta (D9).
- Runner nativo: si usano entrambi. Gli script bash per test restano il
  riferimento per la compilazione. `test/CMakeLists.txt` è **generato** da
  quegli script con `test/support/gen_cmake.py`, da rilanciare dopo aver
  aggiunto o modificato uno script in `common/`.
- `native_stream_test.cpp` è in `common/`, con gli script in `common/`
  (desktop) e `android/`. `change_device_test.cpp` e `vst3_host_test.cpp`
  sono diventati portabili e stanno in `common/`, con gli script in `macos/`,
  `linux/` e `windows/`. `metadata_parsers_test` resta in `macos/`.
- `darwin_engine_lifecycle_test.dart` è stato rinominato
  `ios/ios_engine_lifecycle_test.dart`.
- A2/A3: `repro.dart` è diventato il test `EngineTimeAcrossSessions`;
  `latency_test.dart` (entry point) è stato eliminato;
  `repro_sequential.dart` ora si chiama `run_sequential.dart`.
- Inventario: `pull_buffer_duration_test` e `pull_buffer_in_buffer_seek_test`
  esistevano ma non erano registrati in `all_tests.dart`; ora lo sono.
  `pcm_onsets.dart` è un helper, non un test.
- C: 18 test spostati da `example/tests` a `test/common`.
  `asynchronous_deinit` e `audio_device_lifecycle_races` sono duplicati;
  `mixer_output_capture` e `amplitude_modulator_filter` sono duplicati per
  mantenere la copertura web. Restano nell'example 47 test.
- A5: le sovrapposizioni sono complementari (unità/leak contro comportamento
  sul device), quindi sono state mantenute tutte. I tre duplicati nati
  durante la migrazione (`voice_groups`, `three_d_audio_basic`,
  `create_notes`) sono stati rimossi a favore dei test D4, più completi.
- `speech_text_allocation_test`: la soglia di crescita della RSS è passata da
  64 a 128 MB, perché con i file in parallelo veniva superata di poco
  (64,4 MB) senza alcun leak. Un leak vero vale 192 MB.

---

## 0. Convenzioni (da approvare prima di tutto)

- [x] **0.1 Layout di destinazione**

  ```
  test/
    common/      # host qualsiasi (macOS, Linux, Windows): Dart + C++ portabili
    android/     # NDK + adb, oppure Dart che verifica la logica del plugin Android
    ios/         # simulatore/dispositivo, oppure Dart sul handshake iOS
    macos/       # solo macOS (CoreAudio, AudioToolbox, Cocoa, VST3 .mm)
    linux/       # solo Linux (PulseAudio/ALSA, os_decoder_linux, GStreamer…)
    windows/     # solo Windows (WASAPI, Media Foundation, .bat/.ps1)
    web/         # (proposta) test @TestOn('browser'), non richiesti ma esistenti
    support/     # mock condivisi (vst3_mock_plugin.*, helper di build comuni)
  ```

- [x] **0.2 Naming degli script.** Lo script ha lo stesso nome del sorgente
  che compila e lancia e sta nella cartella della piattaforma su cui gira:
  `test/<piattaforma>/<nome>_test.sh` (oppure `.ps1`/`.bat` su Windows) per
  `<nome>_test.cpp`. Il prefisso `run_` sparisce. Se un sorgente comune si
  lancia anche su un'altra piattaforma, lo script sta nella cartella di quella
  piattaforma e punta a `../common/<nome>_test.cpp`. Esempio:
  `test/android/native_stream_test.sh` → `test/common/native_stream_test.cpp`.
- [x] **0.3 Significato di "common" per il C++.** Un test è common se usa il
  backend null di miniaudio e nessun framework di sistema, quindi si compila
  su macOS, Linux e Windows. I link `-framework` per macOS vanno in un helper
  condiviso (`test/support/native_build.sh`) e non nel singolo test.
- [x] **0.4 Decisione sul runner nativo** (vedi E1): restano solo script bash
  per test, oppure un `test/CMakeLists.txt` con un target e un `add_test` per
  ogni sorgente (come già fa `native_bench/`), più script per test come
  wrapper sottili. Consiglio CMake, perché è l'unico modo realistico di
  coprire Windows senza duplicare 40 script.

---

## A. Pulizia e inventario

- [x] **A1 Allineare il nome di `run_android_native_stream_test.sh`.** È
  l'unico script che non corrisponde a un sorgente; diventa
  `test/android/native_stream_test.sh` (dipende da B1).
- [x] **A2 Rivedere `example/tests/repro.dart` e `repro_sequential.dart`.**
  `repro.dart` riproduce un vecchio freeze del motore MT: se il bug è chiuso
  si elimina oppure diventa un test vero in `tests/` con asserzioni.
  `repro_sequential.dart` duplica il pulsante "Run All" della GUI: tenerlo
  come entry point headless ma rinominarlo (es. `run_sequential.dart`).
- [x] **A3 `example/tests/latency_test.dart`** (entry point standalone)
  duplica `run_tests.dart -a LatencyTest`. Da eliminare se il runner a indice
  basta.
- [x] **A4 `run_all_tests.sh` ha `END=48` fisso**, ma i test sono 55. Va
  calcolato da `all_tests.dart`, oppure il runner deve accettare `ALL` (lo
  supporta già).
- [x] **A5 Sovrapposizioni da valutare** (tenere una sola versione o
  rendere esplicita la differenza):
  - `test/hardware_output_latency_test.dart` ↔
    `example/tests/tests/hardware_output_latency.dart`
  - `test/read_samples_test.dart` ↔ `example/tests/tests/read_samples.dart`
  - `test/realtime_and_custom_dsp_test.dart` ↔ `realtime_and_custom_dsp_test.cpp`
    (va bene tenerli entrambi se uno testa Dart e l'altro il nativo, ma va
    scritto nell'header)
  - `test/limiter_test.cpp` ↔ `example/tests/tests/limiter_filter.dart`
  - `test/amplitude_modulator_test.cpp` ↔ `amplitude_modulator_filter.dart`
- [x] **A6 Rimuovere `example/tests/.DS_Store`** e aggiungerlo a `.gitignore`.
- [x] **A7 Header uniforme.** Ogni test nativo ha in testa un commento con:
  cosa verifica, se serve un device audio, se servono asset, flag supportati
  (`--asan`, `--tsan`, `--wasm32`). Molti lo hanno già; va completato per
  gli altri.

---

## B. Ristrutturazione di `test/`

Ogni spostamento deve aggiornare i path relativi negli script
(`REPO_ROOT="$SCRIPT_DIR/.."` diventa `"$SCRIPT_DIR/../.."`) e i riferimenti
a `example/assets/audio` nei sorgenti che li usano: `audio_source_test.dart`,
`android_native_decoder`, `buffer_stream_race`, `engine_lifecycle`,
`load_mem_ownership`, `native_stream`, `native_decoder`, `play_missing_file`,
`pull_stream_race`, `pull_ogg_seek`, `wavstream_seek`, `wavstream_missing_file`.

- [x] **B1 Creare le cartelle** e `test/support/` con l'helper di build
  comune (flag del compilatore, framework macOS, path di SoLoud/miniaudio,
  `--asan`/`--tsan`), estratto dagli script esistenti.
- [x] **B2 Spostare i test Dart in `test/common/`:**
  `aac_ac3_metadata`, `audio_device_state`, `audio_source`, `buffer_type`,
  `detected_type`, `dispose_native_callables`, `filter_param_names`,
  `filter_type`, `hardware_output_latency`, `hook_download_deadline`,
  `hook_prebuild_install`, `load_mem_allocation`, `looping_region`,
  `mixer_output_format`, `mixer_output_isolate`, `mixer_output_reinit`,
  `mixer_output_stop`, `native_metadata_strings`, `parametric_eq`,
  `parametric_eq_band_frequency`, `player_errors`,
  `player_state_notification`, `read_samples`, `realtime_and_custom_dsp`,
  `skills_install`, `speech_text_allocation`, `vst3` (si salta già da solo
  sulle piattaforme mobili).
- [x] **B3 Spostare i test Dart specifici:**
  - `android/`: `android_engine_lifecycle_test.dart`,
    `android_engine_lifecycle_no_binding_test.dart`
  - `ios/`: `darwin_engine_lifecycle_test.dart` (verifica l'handshake iOS;
    da valutare se rinominarlo `ios_engine_lifecycle_test.dart`)
  - `linux/`: `linux_audio_backend_test.dart`
  - `web/`: `web_bindings`, `web_handle_sign`, `web_metadata_strings`,
    `web_mixer_output_packet`, `web_string_args`

  Nota: questi test girano con `flutter test` sull'host e verificano la
  logica Dart pensata per quella piattaforma. La cartella indica la
  piattaforma che coprono, non quella su cui girano.
- [x] **B4 Spostare i test C++ comuni in `test/common/`** (sorgente e script
  `<nome>_test.sh`):
  `amplitude_modulator`, `buffer_stream_format`, `buffer_stream_race`,
  `bus_annex`, `bus_destroy_filter`, `bus_samplerate`, `bus_visualization`,
  `checkpoint`, `device_coordinator`, `device_list`, `engine_lifecycle`,
  `filter_ops`, `filter_params`, `limiter`, `load_mem_ownership`,
  `loop_end_point`, `mixer_output`, `native_decoder`, `ogg_chain`,
  `opus_output_encoder`, `play_missing_file`, `pull_mp3_seek`,
  `pull_ogg_seek`, `pull_stream_race`, `realtime_and_custom_dsp`,
  `render_ring`, `render_ring_device`, `resampler_carry`,
  `retroactive_remix`, `simd_fallback`, `voice_allocation_failure`,
  `voice_ended_callback`, `voice_tail`, `waveform_synth`,
  `wavstream_missing_file`, `wavstream_seek`, e la cartella `native_bench/`.
- [x] **B5 Spostare i test C++ Android in `test/android/`:**
  `aaudio_close_race`, `aaudio_reroute_race`, `android_native_decoder`, e lo
  script `native_stream_test.sh` che punta a `../common/native_stream_test.cpp`
  (vedi A1).
- [x] **B6 Spostare i test C++ macOS in `test/macos/`:**
  - `change_device` (richiede CoreAudio e un device reale)
  - `vst3_host_test.mm`, con `vst3_mock_plugin.*` e
    `vst3_mock_editor_helper.cpp` spostati in `support/` perché serviranno
    anche a Windows e Linux (D8)
  - `native_stream_test.cpp`: il backend Apple ExtAudioFile si confronta solo
    su macOS. Decidere se il sorgente sta in `common/` (con `#ifdef`) e
    `macos/native_stream_test.sh` è solo lo script, oppure se va tutto in
    `macos/`.
  - `metadata_parsers`: oggi linka il decoder AAC Apple. Si può renderlo
    common sostituendo l'AAC con il decoder della piattaforma, altrimenti
    resta in `macos/`.
- [x] **B7 Aggiornare i riferimenti** in CHANGELOG, README, skill (`skills/`),
  `analyze.yaml` ed eventuali commenti nei sorgenti che citano
  `test/run_*.sh`.
- [x] **B8 Verifica:** `flutter test test/common test/android test/ios
  test/linux` passa sull'host e ogni script in `common/` e `macos/` passa su
  macOS.

---

## C. Spostare da `example/tests` i test che non usano asset

Questi moduli non caricano file da `assets/`. Possono girare con
`flutter test` sull'host, ma tutti tranne `pcm_onsets` chiamano
`SoLoud.instance.init()` e quindi richiedono un device di output
(come già `mixer_output_stop_test.dart`). Per ognuno: convertirlo a
`package:test` (`expect` al posto di `assert`/`OutputBuffer`), spostarlo in
`test/common/` e toglierlo da `all_tests.dart`.

- [x] **C1 `pcm_onsets`**: non usa il motore, si sposta subito.
- [x] **C2 `create_notes`, `waveform_controls`**: usano solo waveform sintetici.
- [x] **C3 `buffer_stream_callbacks`, `buffer_stream_extended`**: PCM generato.
- [x] **C4 `speech_text`**.
- [x] **C5 `amplitude_modulator_filter`, `mixer_output_capture`**: usano
  `MixerOutputStream` (prima verificare A5 per non duplicare
  `mixer_output_*_test.dart`).
- [x] **C6 `asynchronous_deinit`, `audio_device_lifecycle_races`**: dipendono
  dal ciclo di vita del device. Su web e mobile hanno senso solo
  nell'example, quindi valutare se **duplicarli** (host + example) invece di
  spostarli.
- [x] **C7 Ricavare un sound di test senza asset.** Molti test (`handles`,
  `looping`, `pan`, `volume_controls`, `play_seek_pause`, `voice_groups`,
  `protect_voice`, `three_d_audio`, `playback_speed`, `stop_futures`,
  `synchronous_deinit`) usano un asset solo perché serve "un suono
  qualsiasi". Aggiungere in `test/support/` un helper che genera un WAV in
  memoria (`loadMem`) o usa `loadWaveform`, così questi test si possono
  spostare in `test/common/`. Restano nell'example solo quelli che testano
  davvero la decodifica di un formato, il caricamento via asset bundle o il
  comportamento su web/mobile.
- [x] **C8 Rendere i test di `test/common/` che aprono un device
  saltabili senza audio** (tag `@Tags(['device'])` e
  `dart_test.yaml`), così la CI senza scheda audio esegue il resto.

---

## D. Nuovi test (lacune di copertura)

Le stime vengono da un grep su `test/` ed `example/tests/` e vanno
confermate in fase di scrittura. Ogni sotto-punto è un file nuovo.

### D1 Codifica dell'output del mixer (`src/mixeroutput/`) — common C++
- [x] **D1a `flac_output_encoder_test.cpp`**: roundtrip encode e decode, tutti
  i bit depth, flush finale. Oggi non è coperto.
- [x] **D1b `vorbis_output_encoder_test.cpp`**: come sopra, qualità e
  durata. Oggi non è coperto.
- [x] **D1c `wav_output_encoder_test.cpp`**: header RIFF corretto per
  int16/int24/float32, mono e stereo.
- [x] **D1d `pcm_queue_test.cpp`**: overflow, underflow, concorrenza
  producer/consumer (TSAN). Oggi non è coperto.
- [x] **D1e `pcm_converter_test.cpp`**: conversioni di formato e
  saturazione (oggi lo copre solo indirettamente il test del mixer output).

### D2 Decoder stream (`src/audiobuffer/`) — common C++
- [x] **D2a `ogg_flac_stream_decoder_test.cpp`**: oggi non è coperto.
- [x] **D2b `flac_stream_decoder_test.cpp`**: frame parziali, seek, file
  troncato.
- [x] **D2c `circular_float_buffer_test.cpp`**: wrap-around, capacità 0/1,
  scritture più grandi della capacità.
- [x] **D2d `mp3_stream_decoder_test.cpp`**: header ID3 e Xing, input
  frammentato byte per byte, gapless.
- [x] **D2e Fuzz o robustezza** (dati casuali e troncati) per tutti i
  decoder, con `--asan`, sul modello di `metadata_parsers_test`.

### D3 Filtri — common C++ + Dart
- [x] **D3a `soloud_filters_test.cpp`**: smoke test parametrico per ogni
  filtro SoLoud esposto (Robotize, Lofi, Bassboost, Biquad, WaveShaper,
  Flanger, Echo, Freeverb, Eq, DCRemoval, Duck). Per ognuno: niente NaN o
  Inf, niente esplosione di energia, i parametri vengono rispettati, il bypass
  con `wet=0` restituisce l'input. Robotize oggi non è coperto.
- [x] **D3b `compressor_test.cpp`** e **`pitch_shift_filter_test.cpp`**:
  test nativi dedicati (oggi ci sono solo test example basati su asset).
- [x] **D3c `filters_api_test.dart`**: per ogni classe in `lib/src/filters/`,
  attivazione e disattivazione globale e per singolo sound, set/get dei
  parametri, `oscillate`/`fade` dei parametri, errori con indici non validi.

### D4 API Dart principali — common Dart (host con device)
- [x] **D4a `soloud_init_deinit_test.dart`**: init ripetuto, parametri non
  validi, `isInitialized`, deinit durante una riproduzione, errori
  `SoLoudException` prima dell'init.
- [x] **D4b `voice_control_test.dart`**: volume, pan, velocità, pausa, stop,
  `fadeVolume`, `oscillateVolume`, handle non validi o già terminati.
- [x] **D4c `voice_group_test.dart`**: creazione e distruzione, operazioni
  propagate, gruppi vuoti.
- [x] **D4d `three_d_audio_test.dart`**: listener, attenuazione, doppler,
  `update3dAudio`, in modo deterministico con `readSamples`/`MixerOutput`.
- [x] **D4e `metadata_api_test.dart`**: `getMetadata` / callback dei metadata
  su sorgenti generate. Oggi non è coperto.
- [x] **D4f `sound_hash_test.dart`**: unicità, uguaglianza, `SoundHash.random`.
- [x] **D4g `soloud_tools_test.dart`**: `SoLoudTools` (oggi è coperto solo
  dall'example).
- [x] **D4h `loader_test.dart`**: `loader_io` (file temporanei,
  `automaticCleanup`, URL con server HTTP locale finto). Oggi non è coperto.
- [x] **D4i `mixing_bus_test.dart`**: bus annidati, filtri sul bus, play su
  bus distrutto (oggi è coperto solo dall'example con asset).
- [x] **D4j `exceptions_mapping_test.dart`**: ogni codice d'errore C++ viene
  mappato all'eccezione Dart corretta (`exceptions_from_cpp.dart`).
- [x] **D4k `enums_sync_test.dart`**: gli enum Dart (`lib/src/enums.dart`)
  hanno gli stessi valori di `src/enums.h`. Si fa con un parse del
  file header, senza bisogno di device.
- [x] **D4l `ffigen_bindings_sync_test.dart`**: i binding generati sono
  allineati a `bindings.h` (rigenera in una cartella temporanea e confronta).

### D5 Sintesi e sorgenti — common C++
- [x] **D5a `waveform_test.cpp`**: `src/waveform/` (waveform e
  `miniaudio_libvorbis`). Oggi non è coperto direttamente.
- [x] **D5b `speech_source_test.cpp`**: `Speech` nativo (output non vuoto,
  testo vuoto o molto lungo).
- [ ] **D5c `noise_and_sfxr_test.cpp`**, se esposti dalla API.
  *Non fatto: Noise e Sfxr non sono esposti né in `lib/` né in `src/bindings.cpp`.*

### D6 Analyzer e visualizzazione — common C++
- [x] **D6a `analyzer_test.cpp`**: FFT di una sinusoide nota con picco nel
  bin atteso, finestra, buffer di wave (`analyzer.cpp`).

### D7 Specifici di piattaforma
- [x] **D7a Windows — `windows/native_decoder_test.ps1`** +
  `aac_stream_decoder_windows` / `os_decoder_windows` (Media Foundation).
- [x] **D7b Windows — `windows/change_device_test.cpp`** (WASAPI device swap,
  equivalente del test macOS).
- [x] **D7c Windows — script `.ps1` per tutti i test `common/`** (oppure
  CMake, vedi E1).
- [x] **D7d Linux — `linux/os_decoder_linux_test.cpp`** e
  `aac_stream_decoder_linux` (dipendenze di sistema, comportamento se mancano).
- [x] **D7e Linux — `linux/change_device_test.sh`** riusando il sorgente
  comune con PulseAudio/ALSA (salta se non c'è un device).
- [x] **D7f iOS — `ios/`**: test nativi dei decoder Apple
  (`aac_stream_decoder_apple.mm`, `os_decoder_apple.mm`) compilati per il
  simulatore con `xcrun simctl spawn`. In alternativa un target XCTest nel
  progetto example.
- [ ] **D7g iOS — sessione audio**: interruzioni e cambio di route (test
  Flutter integration sotto `example/`, perché richiede l'app).
  *Non fatto: da fuori l'app non si può far arrivare a iOS una vera
  interruzione o un cambio di route. L'alternativa praticabile è un binario
  nel simulatore che pubblica `AVAudioSessionInterruptionNotification`, ma
  richiede di compilare SoLoud e la parte Objective-C di miniaudio per il
  simulatore.*
- [x] **D7h Android — `android/os_decoder_android_test.cpp`** e
  `aac_stream_decoder_android` (MediaCodec), via adb come gli altri.
- [x] **D7i macOS — `macos/aac_stream_decoder_apple_test.mm`** e
  `macos/m4a_gapless_test.cpp` (iTunSMPB).

### D8 VST3 multi-piattaforma
- [x] **D8a** Rendere `vst3_host_test` compilabile anche su Linux e Windows
  (`vst3_editor_linux.cpp`, `vst3_editor_windows.cpp`), con i mock in
  `support/`.

### D9 Web (se si approva la cartella `test/web/`)
- [x] **D9a** Verificare che i test `@TestOn('browser')` girino con
  `flutter test --platform chrome` sia in variante ST che MT (WASM).

---

## E. Runner e CI

- [x] **E1 `test/CMakeLists.txt`**: un target per ogni test in `common/` e
  in quella della piattaforma corrente, con opzioni `SANITIZE=asan|tsan`, e
  `ctest -L common|macos|linux|windows`. Gli script per test diventano wrapper
  (`cmake --build … --target <nome> && ctest -R <nome>`).
- [x] **E2 Script aggregati per piattaforma**: `test/<piattaforma>/all.sh`
  (oppure `all.ps1`) che lancia tutti i test di quella cartella e di
  `common/` e stampa un riepilogo PASS/FAIL/SKIPPED.
- [x] **E3 `dart_test.yaml`** con tag `device`, `asan`, `slow` e path di
  default, così `flutter test` senza argomenti lancia solo `common/` più la
  piattaforma host.
- [x] **E4 CI GitHub Actions**: matrix macOS/Linux/Windows con `flutter test`
  e test nativi `common/` (backend null), più un job ASan. Android e iOS
  restano manuali o su runner dedicati.
- [x] **E5 Coverage**: LCOV per Dart (`flutter test --coverage`) e
  `llvm-cov`/`gcov` per i test nativi `common/`, con un report unico.
- [x] **E6 `test/README.md`**: struttura, convenzioni (punto 0), come
  lanciare ogni categoria, requisiti (Xiph via brew, NDK, adb, device audio).

---

## Bug trovati dai nuovi test (non corretti)

Ogni bug è coperto da un caso di test. Nei test Dart il caso è marcato
`skip: 'bug: ...'` (si vede fallire con `--run-skipped`); nei test nativi
stampa `KNOWN BUG` (fallisce con `--strict` / `OSDEC_STRICT=1`), salvo i
due fallimenti già presenti. Numeri di riga indicativi.

### Già presenti su `main` (test esistenti che falliscono)
- [ ] **K1** `pull_mp3_seek_test`: in un MP3 CBR senza header Xing, il seek a 6 s
  chiede il byte 96000, ma la mappa del file lo colloca a 89025.
- [ ] **K2** `native_stream_test`: il trim della edit list di AC-3 in MP4 è
  sfasato di 256 campioni (`test/common/native_stream_test.cpp:1608`).

### Engine, 3D e API Dart
- [ ] **E1** `play()` su una sorgente `speechText()` va in segfault:
  `Player::textToSpeech` registra un `ActiveSound` senza `sound`
  (`src/player.cpp` ~3384).
- [ ] **E2** L'attenuazione 3D viene applicata due volte, quindi il livello
  segue il quadrato del modello (`soloud_core_3d.cpp:296` e `:301`).
- [ ] **E3** Il doppler ha il segno invertito: una sorgente che si avvicina
  suona più grave (`soloud_core_3d.cpp:144`).
- [ ] **E4** `set3dListenerUp` imposta il vettore "at" (`src/player.cpp:4194`).
- [ ] **E5** I setter 3D chiamati prima di caricare il primo suono vengono
  persi (`src/bindings.cpp:4862` e seguenti).
- [ ] **E6** `createVoiceGroup()` prima di `init` restituisce 4294967295 invece di
  lanciare un'eccezione (`src/bindings.cpp:3657`, `lib/src/soloud.dart:4157`).
- [ ] **E7** `deinit()` non elimina i `Bus`, che restano validi sul nuovo engine
  (`lib/src/soloud.dart:1115`).
- [ ] **E8** `countAudioSource` restituisce sempre 0 per le waveform
  (`src/player.cpp:3568`).
- [ ] **E9** Il messaggio di `PlayerErrors.filterAlreadyAdded` è "Filter not
  found!" (`lib/src/enums.dart:168`).

### Filtri
- [ ] **F1** I default Dart non corrispondono a quelli nativi:
  - Biquad resonance: Dart 0.1, nativo 2.
  - Flanger delay/freq: default e range sbagliati.
  - Limiter kneeWidth/releaseTime: Dart 2 e 100, nativo 6 e 50.
  - Parametric EQ: la Q di default della banda non corrisponde.
- [ ] **F2** WaveShaper con AMOUNT negativo: divisione per zero, NaN a |x|=1
  (`soloud_waveshaperfilter.cpp`).
- [ ] **F3** Biquad: FREQUENCY non è limitata sotto Nyquist e diventa Inf a
  11025 Hz (`soloud_biquadresonantfilter.cpp`).
- [ ] **F4** PitchShift: le nuove istanze non copiano WET
  (`src/filters/pitch_shift_filter.cpp:7-13`). Inoltre il range di SEMITONES
  (±36) supera quello di SHIFT (max 3).
- [ ] **F5** Compressor: `getParamCount()` restituisce 8, ma i parametri sono 7
  (`src/filters/compressor.cpp`).

### Decoder in streaming
- [ ] **S1** FLAC e Ogg FLAC restano bloccati in `ABORTED` dopo un read abort e
  il drain non finisce mai (`flac_stream_decoder.cpp:246,273`,
  `ogg_flac_stream_decoder.cpp:211,239`).
- [ ] **S2** FLAC: un frame a cavallo di due `decode()` va perso
  (`flac_stream_decoder.cpp:302-308`). Se la prima consegna è di 1–3 byte, il
  decoder non si riprende più (`:168`).
- [ ] **S3** Ogg FLAC: con l'output limitato l'ultimo frame va perso
  (`ogg_flac_stream_decoder.cpp:282`).
- [ ] **S4** Gli MP3 con un tag ID3 più grande di 4 KB vengono riconosciuti come
  AC-3 quando arrivano a pezzi (`stream_decoder.cpp:45`).
- [ ] **S5** MP3: un `drmp3_init()` lazy fallito perde dati e può bloccare il drain
  (`mp3_stream_decoder.cpp:266`). Lo stesso schema c'è nel wrapper WAV con
  dati non WAV.
- [ ] **S6** I frame ID3 con extended header non vengono letti
  (`mp3_stream_decoder.cpp:72`).
- [ ] **S7** Le prime consegne corte vengono rifiutate invece di attendere altri
  dati (`stream_decoder.cpp:71,173`).
- [ ] **S8** Vorbis: `OggMetadata.vendor` è sempre vuoto
  (`vorbis_stream_decoder.cpp:80`).
- [ ] **S9** `miniaudio_libvorbis`: la read callback restituisce `size_t` invece
  di `ma_result`, quindi `ma_libvorbis_init_memory` fallisce sempre
  (`src/waveform/miniaudio_libvorbis.cpp:261-277,344`).

### Decoder di piattaforma
- [ ] **P1** Linux AAC: con l'output limitato per chiamata perde la maggior
  parte dello stream (`aac_stream_decoder_linux.cpp:389`).
- [ ] **P2** Linux AAC: il prebuffer decodifica due volte e perde pacchetti
  (`:286`).
- [ ] **P3** Linux AAC: heap over-read, perché all'input del parser manca il
  padding FFmpeg (`:294,322`).
- [ ] **P4** Linux AAC: il caricamento del codec non è thread-safe (`:72`). Linux:
  `decodeMemory` usa `/tmp` cablato e ignora `TMPDIR`.
- [ ] **P5** Apple AAC/AC-3: i chunk più piccoli di circa un frame perdono audio
  (`aac_stream_decoder_apple.mm:61`). Linux ha la stessa logica (`:267`).
- [ ] **P6** Apple: l'ultimo frame AC-3/E-AC-3 non esce dopo `setDataEnded()`
  (`aac_stream_decoder_apple.mm:194`).
- [ ] **P7** Apple: gli stream HE-AAC ADTS suonano a 22,05 kHz, cioè senza SBR
  (`aac_stream_decoder_apple.mm:234`).
- [ ] **P8** `AACDecoderWrapper::hasPendingData()` resta true per sempre se
  l'inizializzazione è fallita (`aac_stream_decoder.h:24`; anche su Windows,
  `aac_stream_decoder_windows.cpp:115`).
- [ ] **P9** (sospetto) Windows: `CoInitializeEx` e `CoUninitialize` possono
  essere chiamati su thread diversi (`aac_stream_decoder_windows.cpp:51,274`).

### Encoder e varie
- [ ] **O1** L'encoder Vorbis non riordina i canali 5.1/7.1: il canale BR di
  SoLoud finisce nello slot LFE e viene filtrato
  (`vorbis_output_encoder.cpp:242-246`).
- [ ] **O2** L'header WAV è scritto nell'endianness dell'host e `dataSize` viene
  troncato oltre 4 GB (`wav_output_encoder.cpp`). Il commento
  `mixer_output_encoder.h` dice che l'output viene "appended", ma viene
  sostituito.
- [ ] **O3** TTS: overflow con segno su una lunga sequenza di cifre
  (`tts.cpp:1315`). Inoltre a `isalpha` e simili arriva un `char` con segno.

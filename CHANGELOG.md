### 2.1.0
- added `getPan()`, `setPan()` and `setPanAbsolute()`.
- added support for the Web platform.
- added `loadMem()` to read the give audio file bytes buffer (not RAW data). Useful for the Web platform.
- fixed `getFilterParamNames()`.
- added `AudioData` class to manage audio samples.
- added player initialization parameters: sample rate, buffer size, number of channels (mono, stereo, quad, 5.1, 7.1).
- added voice groups.
- it's now possible to set filters not only globally, but also to single audio sources.
- fade and oscillate filter parameters.
- experimental capture feature removed.

### 2.0.2 (23 May 2024)
- Fixed wrong exception raised by `setVolume()` when a handle is no more valid.

### 2.0.1 (6 May 2024)
- Fix init error on hot restart.

### 2.0.0 (5 Apr 2024)
- A giant leap forward from the previous version (many thanks to Filip Hráček).
- Major changes to API. There are quick fixes (`dart fix`) to automatically rename many changed APIs.
- `SoLoud` methods now throw instead of returning a PlayerErrors object.
- added `getActiveVoiceCount()` to get concurrent sounds that are playing at the moment.
- added `countAudioSource()` to get concurrent sounds that are playing a specific audio source.
- added `getVoiceCount()` to get the number of voices the application has told SoLoud to play.
- added `getMaxActiveVoiceCount()` to get the current maximum active voice count.
- added `setMaxActiveVoiceCount()` to set the current maximum active voice count.
- added `setProtectVoice()` and `getProtectVoice()` to get/set the protect voice flag.
- `SoLoud.activeSounds` is now an `Iterable` instead of a `List`.
- All time-related parameters and return values are now `Duration` type.
  Before, they were `double`.
- Added new (experimental) `AudioSource.allInstancesFinished` stream. 
  This can be used to more easily await times when it's safe to dispose 
  the sound. For example:

  ```dart
  final source = soloud.loadAsset('...');
  // Wait for the first time all the instances of the sound are finished
  // (finished playing or were stopped with soloud.stop()).
  source.allInstancesFinished.first.then(
    // Dispose of the sound.
    (_) => soloud.disposeSound(source)
  );
  soloud.play(source);
  ```
- added `looping` and `loopingStartAt` properties to `SoLoud.play()` and `SoLoud.play3d()`.
- added `SoLoud.getLooping()` to retrieve the looping state of a sound.
- added `SoLoud.getLoopPoint()` and `SoLoud.setLoopPoint()` to get and set the looping start position of a sound.
- New methods `SoLoud.loadAsset()` and `SoLoud.loadUrl()` to load audio from assets and URLs, respectively.
- added `mode` property to `SoLoud.loadFile()` and `SoloudTools.loadFrom*` to prevent to load the whole audio data into memory:
    - *LoadMode.memory* by default. Means less CPU, more memory allocated.
    - *LoadMode.disk* means more CPU, less memory allocated. Lags can occurs while seeking MP3s, especially when using a slider.
- Switched from `print()` logging to using the standard `package:logging`.
  See `README.md` to learn how to capture log messages and how to filter them.
- The capture feature is on experimental stage to be fine tuned in the near future. All methods related to audio capture have been extracted to a separate class. 
  So now, there are two classes:
    - `SoLoud` for _playing_ audio
    - `SoLoudCapture` for _capturing_ audio
- The Web platform is a work in progress, stay tuned!
- Switched LICENSE from Apache-2.0 to MIT.

### 2.0.0-pre.5 (4 Apr 2024)
- getLoopPoint now returns Duration.
- Major changes to API docs and README.
- Renamed `SoLoud.disposeSound` to `SoLoud.disposeSource`.
  Quick fix available.
- Renamed `SoLoud.disposeAllSound` to `SoLoud.disposeAllSources`.
  Quick fix available.
- Removed unused `AudioSource.keys` property.
- Switched LICENSE from Apache-2.0 to MIT.

### 2.0.0-pre.4 (21 Mar 2024)
- some little fixes.

### 2.0.0-pre.3 (20 Mar 2024)
- added `getActiveVoiceCount()` to get concurrent sounds that are playing at the moment.
- added `countAudioSource()` to get concurrent sounds that are playing a specific audio source.
- added `getVoiceCount()` to get the number of voices the application has told SoLoud to play.
- added `getMaxActiveVoiceCount()` to get the current maximum active voice count.
- added `setMaxActiveVoiceCount()` to set the current maximum active voice count.
- added `setProtectVoice()` and `getProtectVoice()` to get/set the protect voice flag.
- All time-related parameters and return values are now `Duration` type.
  Before, they were `double`.
- Fixed velocity computation bug in `example/`.
- Renamed `SoundEvent` to `SoundEventType`. Quick fix available.
- `SoundProps.soundEvents` is now a `Stream`, not a `StreamController`
- `SoundProps.soundEvents` stream is now closed automatically when
  `SoLoud.disposeSound()` is called.
- `SoLoud.activeSounds` is now an `Iterable` instead of a `List`
  (therefore, it cannot be modified from outside the package).
- Renamed `SoLoud.getFxParams` to `SoLoud.getFilterParameter`.
  This mimics the C++ API name.
  Quick fix available.
- Renamed `SoLoud.setFxParams` to `SoLoud.setFilterParameter`. 
  This mimics the C++ API name.
  Quick fix available.
- Renamed `SoundProps` to `AudioSource`. Quick fix available.
- Added new (experimental) `AudioSource.allInstancesFinished` stream. 
  This can be used to more easily await times when it's safe to dispose 
  the sound. For example:

  ```dart
  final source = soloud.loadAsset('...');
  // Wait for the first time all the instances of the sound are finished
  // (finished playing or were stopped with soloud.stop()).
  source.allInstancesFinished.first.then(
    // Dispose of the sound.
    (_) => soloud.disposeSound(source)
  );
  soloud.play(source);
  ```
- Deprecated `shutdown()`. Replaced with the synchronous `deinit()`.
  Quick fix available.
- Renamed `initialize()` to `init()`, in order to come closer to the original
  C++ API, and also to have a symmetry (`init`/`deinit`).
  Quick fix available.

### 2.0.0-pre.2 (14 Mar 2024)

NOTE: This version is much more breaking than the ones before it.
It might be worth it to first upgrade your code to `2.0.0-pre.1`,
use the quick fixes to rename the methods, and only then upgrade 
to `2.0.0-pre.2` and beyond.

- `SoLoud` methods now throw instead of returning a `PlayerErrors` object.
  This is a massive breaking change, but it makes the package API
  more idiomatic and easier to use.
  
  Before:

  ```dart
  final ret = await SoLoud.play(sound);
  if (ret.error != PlayerErrors.noError) {
    print('Oh no! ${ret.error}');
  } else {
    print('Playing sound with new handle: ${ret.newHandle}');
  }
  ```

  After:

  ```dart
  try {
    final handle = await SoLoud.play(sound);
    print('Playing sound with new handle: $handle');
  } on SoLoudException catch (e) {
    print('Oh no! $e');
  }
  ```

### 2.0.0-pre.1 (12 Mar 2024)
- added `looping` and `loopingStartAt` properties to `SoLoud.play()` and `SoLoud.play3d()`.
- added `SoLoud.getLooping()` to retrieve the looping state of a sound.
- added `SoLoud.getLoopPoint()` and `SoLoud.setLoopPoint()` to get and set the looping start position of a sound.
- New methods `SoLoud.loadAsset()` and `SoLoud.loadUrl()` to load audio
  from assets and URLs, respectively. These replace the old
  `SoloudTools.loadFrom*` methods (which are now deprecated).
  - The new methods also correctly invalidate the temporary files
    (for example, when an asset changes between versions of the app,
    we don't want to play the old file).
- Rename `SoloudTools` to `SoLoudTools` for consistency. (Quick fix available.)
- Rename `SoLoudTools.initSounds` to `SoLoudTools.createNotes` for clarity.
  (Quick fix available.)

### 2.0.0-pre.0 (11 Mar 2024)
- added `bool SoLoud.getVisualizationEnabled()` to get the current state of the visualization.
- added `mode` property to `SoLoud.loadFile()` and `SoloudTools.loadFrom*` to prevent to load the whole audio data into memory:
    - *LoadMode.memory* by default. Means less CPU, more memory allocated.
    - *LoadMode.disk* means more CPU, less memory allocated. Lags can occurs while seeking MP3s, especially when using a slider.
- Switched from `print()` logging to using the standard `package:logging`.
  See `README.md` to learn how to capture log messages and how to filter
  them.
- Renamed `SoLoud.startIsolate()` to `SoLoud.initialize()`
- Renamed `SoLoud.stopIsolate()` to `SoLoud.shutdown()`
- Removed `SoLoud.initEngine()` (it shouldn't be called manually)
- None of the renaming changes are strictly breaking (yet). 
  The old method names still exist as aliases to the new names, and are
  merely marked `@deprecated`. There is a quick fix (`dart fix`) 
  to automatically rename them.
- The singleton SoLoud instance is now accessible through `SoLoud.instance`.
  Accessing it through `SoLoud()` is now deprecated.
    - This change cannot be automated through a Quick Fix. 
      You will need to manually replace `SoLoud()` with `SoLoud.instance`
      in your code.
- All methods related to audio capture have been extracted to a separate class. 
  So now, there are two classes:
    - `SoLoud` for _playing_ audio
    - `SoLoudCapture` for _capturing_ audio
- The `SoLoud` class is now an `interface` class.
  This means you can _implement_ it (e.g. for mocking in tests) but you can't
  _extend_ it. This reduces the
  [fragile base class problem](https://en.wikipedia.org/wiki/Fragile_base_class)
  and makes the API easier to evolve.
- Added a new, more usable way of finding out whether the audio engine
  is initialized and ready to use:
    - `SoLoud.initialized` (returns a future, safe to check during initialization)
      - This is a much easier way to check engine readiness than
        subscribing to `SoLoud.audioEvents` and waiting for 
        the `isolateStarted` event.
    - `SoLoud.isInitialized` (returns synchronously)
    - previous methods to check readiness (`isPlayerInited` and `isIsolateRunning()`)
      are now deprecated
- `SoLoud.initialize()` can now be safely called during engine
  shutdown. It will wait for the engine to shut down before
  re-initializing it. Same for `SoLoud.shutdown()`, which will 
  wait for the engine to initialize before shutting it down,
  to avoid various race conditions.
- Sound handles and sound hashes are now typed: `SoundHandle` and `SoundHash`
  instead of raw integers.
  This prevents from erroneously passing a sound handle as a sound hash,
  for example. This is a breaking API change but, in practice, shouldn't
  be much of a problem, since these objects were always meant as
  identifiers (to be taken from some API calls and put into others).
- `SoundProps.handle` renamed to `SoundProps.handles` (because it's a Set)
  and also disallowed modifying it from outside the package.
- All fields of `SoundProps` marked `final`. This is a breaking change
  but unlikely to have effect (as most users hopefully don't assign
  to these fields).

### 1.2.5 (2 Mar 2024)
- updated mp3, flac and wav decoders
- updated miniaudio to 0.11.21
- fixed doppler effect in 3D audio example

### 1.2.4
fixed compilation on Windows

### 1.2.3
- fixed compilation on iOS and macOS

### 1.2.2
- waveform example page updated with sound FXs
- added sound FXs
    - biquadResonantFilter
    - eqFilter
    - echoFilter
    - lofiFilter
    - flangerFilter
    - bassboostFilter
    - waveShaperFilter
    - robotizeFilter
    - freeverbFilter

### 1.2.1
- binded some more SoLoud functionalities:
    - fadeGlobalVolume
    - fadeVolume
    - fadePan
    - fadeRelativePlaySpeed
    - schedulePause
    - scheduleStop
    - oscillateVolume
    - oscillatePan
    - oscillateRelativePlaySpeed
    - oscillateGlobalVolume
- waveform example page updated

### 1.2.0
- added waveform generator
- added a test page for waveform
- added some tests in `tests` dir
- miniaudio updated to v0.11.18

### 1.1.1
- *SoLoud().loadFile* now can return *PlayerErrors.fileAlreadyLoaded* when a sound has already been loaded previously. It still return the SoundProps sound. It's not a breaking error.
- added *Soloud().disposeAllSound* to stop and dispose all active sounds

**breaking change**: *Soloud().stopSound* has been renamed to *Soloud().disposeSound*

### 1.1.0
added load sound tools:
- SoloudLoadingTool.loadFromAssets()
- SoloudLoadingTool.loadFromFile()
- SoloudLoadingTool.loadFromUrl()

added also a spin around example

### 1.0.0
- added 3D audio with example

### 0.9.0
- added capture from microphone with example

### 0.1.0

Initial release:
* Supported on Linux, Windows, Mac, Android, and iOS
* Multiple voices, capable of playing different sounds simultaneously or even repeating the same sound multiple times on top of each other
* Includes a speech synthesizer
* Supports various common formats such as 8, 16, and 32-bit WAVs, floating point WAVs, OGG, MP3, and FLAC
* Enables real-time retrieval of audio FFT and wave data


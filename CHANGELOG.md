#### 1.2.xx
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

#### 1.2.5 (2 Mar 2024)
- updated mp3, flac and wav decoders
- updated miniaudio to 0.11.21
- fixed doppler effect in 3D audio example

## 1.2.4
fixed compilation on Windows

#### 1.2.3
- fixed compilation on iOS and macOS

#### 1.2.2
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

#### 1.2.1
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

#### 1.2.0
- added waveform generator
- added a test page for waveform
- added some tests in `tests` dir
- miniaudio updated to v0.11.18

#### 1.1.1
- *SoLoud().loadFile* now can return *PlayerErrors.fileAlreadyLoaded* when a sound has already been loaded previously. It still return the SoundProps sound. It's not a breaking error.
- added *Soloud().disposeAllSound* to stop and dispose all active sounds

**breaking change**: *Soloud().stopSound* has been renamed to *Soloud().disposeSound*

#### 1.1.0
added load sound tools:
- SoloudLoadingTool.loadFromAssets()
- SoloudLoadingTool.loadFromFile()
- SoloudLoadingTool.loadFromUrl()

added also a spin around example

#### 1.0.0
- added 3D audio with example

#### 0.9.0
- added capture from microphone with example

#### 0.1.0

Initial release:
* Supported on Linux, Windows, Mac, Android, and iOS
* Multiple voices, capable of playing different sounds simultaneously or even repeating the same sound multiple times on top of each other
* Includes a speech synthesizer
* Supports various common formats such as 8, 16, and 32-bit WAVs, floating point WAVs, OGG, MP3, and FLAC
* Enables real-time retrieval of audio FFT and wave data


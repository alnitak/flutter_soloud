#### 1.X.Y
- added `loadIntoMem` property to `Soloud.loadFile()` to prevent to load the whole audio data into memory:
    - *true* by default. Means less CPU, more memory allocated.
    - *false* means more CPU, less memory allocated. By seeking backward an mp3 some lags can occurs, especially when using a slider.

#### 1.2.4
fixed compilation on Windows

#### 1.2.3
fixed compilation on iOS and macOS

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

#### [...]

#### 0.1.0

Initial release:
* Supported on Linux, Windows, Mac, Android, and iOS
* Multiple voices, capable of playing different sounds simultaneously or even repeating the same sound multiple times on top of each other
* Includes a speech synthesizer
* Supports various common formats such as 8, 16, and 32-bit WAVs, floating point WAVs, OGG, MP3, and FLAC
* Enables real-time retrieval of audio FFT and wave data


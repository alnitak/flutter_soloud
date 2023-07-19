# Flutter audio plugin using SoLoud library

Flutter audio plugin using SoLoud library and FFI

[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)
|Linux|Windows|Android|MacOS|iOS|web|
|-|-|-|-|-|-|
|💙|💙|💙|💙|💙|😭|

## Overview

***flutter_soloud*** plugin uses [SoLoud](https://github.com/jarikomppa/soloud) forked repo where [miniaudio](https://github.com/mackron/miniaudio) audio backend has been updated and it is located in `src/soloud` as a git  submodule.

So it is mandatory to clone this repo using:
```git clone --recursive https://github.com/alnitak/flutter_soloud.git```

If you already cloned normally, go into the repo dir and:
```git submodule update --init --recursive```

For the SoLoud license look at [here](https://github.com/alnitak/soloud/blob/f4f089aa592aa45f5f6fa8c8efff64996fae920f/LICENSE).

The are 3 examples:
***The 1st*** is a simple use-case.

***The 2nd*** aims to show a visualization of frequencies and wave data.
[***Visualizer.dart***] uses `getAudioTexture2D` to store new audio data into `audioData` on every Tick.

The video below illustrate how the data is then converted to an image (the upper widget) and then it's sent to the shader (the middle widget).
The bottom widgets use, on the left, FFT data and on the right the wave data represented with a row of yellow vertical containers with the height taken from `audioData`.

The `getAudioTexture2D` returns an array of 512x256. Each row contains 256 Floats of FFT data and 256 Floats of wave data making it possible to write a shader like a spectogram (shader #8) or a 3D visualization (shader #9).

The shaders from 1 to 7 are using just 1 row of the `audioData`. Therefore the texture generated to feed the shader, should be 256x2 px. The 1st row represents the FFT data, and the 2nd the wave data.


https://github.com/alnitak/flutter_soloud/assets/192827/fe0f16a2-eecd-47d0-aee2-71411833e0d5

***The 3rd*** example shows how to manage sounds by its handles: every sounds should be loaded before they can be played. Loading a sound can take some time and should not be done ie in a game. After loading a sound it can be played and every instance of that same audio, will be indentified byt its *handle*.
The example show you can have a background music and play a fire sound multiple times.


## Usage
First of all, *AudioIsolate* must be initialized:
```
Future<bool> start() async{
  final value = AudioIsolate().startIsolate();
  if (value == PlayerErrors.noError) {
    debugPrint('isolate started');
    return true;
  } else {
    debugPrint('isolate starting error: $value');
    return false;
  }
}
```
When succesfully started a sound can be loaded:
```
Future<SoundProps?> loadSound(String completeFileName) {
  final load = await AudioIsolate().loadFile(completeFileName);
  if (load.error != PlayerErrors.noError) return null;
  return load.sound;
}
```
The [SoundProps] returned:
```
class SoundProps {
  SoundProps(this.soundHash);

  // the [hash] returned by [loadFile]
  final int soundHash;

  /// handles of this sound. Multiple instances of this sound can be
  /// played, each with their unique handle
  List<int> handle = [];

  /// the user can listed ie when a sound ends or key events (TODO)
  StreamController<StreamSoundEvent> soundEvents = StreamController.broadcast();
}
```
*soundHash* and *handle* list are then used in the *AudioIsolate()* class.

### The AudioIsolate instance

The AudioIsolate instance has the duty to receive commands and send them to a separate Isolate giving back to the main UI isolate the results.

|function|returns|params|description|
|-|-|-|-----------------------|
|***startIsolate***|PlayerErrors|-|Start the audio isolate and listen for messages coming from it.|
|***stopIsolate***|bool|-|Stop the loop, stop the engine and kill the isolate. Must be called when there is no more need of the player or when closing the app.|
|***isIsolateRunning***|bool|-|Return true if the audio isolate is running.|
|***initEngine***|PlayerErrors|-|Initialize the audio engine. Defaults are:<br/><br/>*Miniaudio* audio backend<br/>sample rate 44100<br/>buffer 2048|
|***dispose***|-|-|Stop the audio engine.|
|***loadFile***|({PlayerErrors error, SoundProps? sound})|`String` fileName|Load a new sound to be played once or multiple times later.|
|***play***|({PlayerErrors error, SoundProps sound, int newHandle})|`SoundProps` sound, {<br/>`double` volume = 1,<br/>`double` pan = 0,<br/>`bool` paused = false,<br/>}|Play already loaded sound identified by [sound].|
|***speechText***|({PlayerErrors error, SoundProps sound})|`String` textToSpeech|Speech given text.|
|***pauseSwitch***|PlayerErrors|`int` handle|Pause or unpause already loaded sound identified by [handle].|
|***getPause***|({PlayerErrors error, bool pause})|`int` handle|Gets the pause state of the sound identified by [handle].|
|***stop***|PlayerErrors|`int` handle|Stop already loaded sound identified by [handle] and clear it.|
|***stopSound***|PlayerErrors|`int` handle|Stop ALL handles of the already loaded sound identified by [soundHash] and clear it.|
|***getLength***|({PlayerErrors error, double length})|`int` soundHash|Gets the sound length in seconds.|
|***seek***|PlayerErrors|`int` handle, `double` time|Seek playing in seconds.|
|***getPosition***|({PlayerErrors error, double position})|`int` handle|Get current sound position in seconds.|
|***getIsValidVoiceHandle***|({PlayerErrors error, bool isValid})|`int` handle|Check if a handle is still valid.|
|***setVisualizationEnabled***|-|`bool` enabled|Enable or disable getting data from `getFft`, `getWave`, `getAudioTexture*`|
|***getFft***|-|`Pointer<Float>` fft|Returns a 256 float array containing FFT data.|
|***getWave***|-|`Pointer<Float>` wave|Returns a 256 float array containing wave data (magnitudes).|
|***getAudioTexture***|`Pointer<Float>` samples|-|Returns in `samples` a 512 float array.<br/>- The first 256 floats represent the FFT frequencies data [0.0~1.0].<br/>- The other 256 floats represent the wave data (amplitude) [-1.0~1.0].|
|***getAudioTexture2D***|-|`Pointer<Pointer<Float>>` samples|Return a floats matrix of 256x512.<br/>Every row are composed of 256 FFT values plus 256 wave data.<br/>Every time is called, a new row is stored in the first row and all the previous rows are shifted up (the last will be lost).|
|***setFftSmoothing***|-|`double` smooth|Smooth FFT data.<br/>When new data is read and the values are decreasing, the new value will be decreased with an amplitude between the old and the new value.<br/> This will result on a less shaky visualization.<br/>0 = no smooth<br/>1 = full smooth<br/>the new value is calculated with:<br/>`newFreq = smooth * oldFreq + (1 - smooth) * newFreq`|

The `PlayerErrors` enum:
|name|description|
|---|---|
|***noError***|No error|
|***invalidParameter***|Some parameter is invalid|
|***fileNotFound***|File not found|
|***fileLoadFailed***|File found, but could not be loaded|
|***dllNotFound***|DLL not found, or wrong DLL|
|***outOfMemory***|Out of memory|
|***notImplemented***|Feature not implemented|
|***unknownError***|Other error|
|***backendNotInited***|Player not initialized|
|***nullPointer***|null pointer. Could happens when passing a non initialized pointer (with calloc()) to retrieve FFT or wave data|
|***soundHashNotFound***|The sound with specified hash is not found|
|***isolateAlreadyStarted***|Audio isolate already started|
|***isolateNotStarted***|Audio isolate not yet started|
|***engineNotStarted***|Engine not yet started|

*AudioIsolate()* has a `StreamController` which can be used for now to know when a sound handle reached the end:
```
StreamSubscription<StreamSoundEvent>? _subscription;
void listedToEndPlaying(SoundProps sound) {
  _subscription = sound!.soundEvents.stream.listen(
    (event) {
      /// Here the [event.handle] of [sound] has naturally finished
      /// and [sound.handle] doesn't contains [envent.handle] anymore.
      /// Not passing here when calling [AudioIsolate().stop()]
      /// or [AudioIsolate().stopSound()]
    },
  );
}
```
it has also a `StreamController` to monitor when the engine starts or stops:
```
AudioIsolate().audioEvent.stream.listen(
  (event) {
    /// event == AudioEvent.isolateStarted
    /// or
    /// event == AudioEvent.isolateStopped
  },
);
```

## Contribute

To use the native code, bindings from Dart to c/c++ is needed.
To avoid writing these by hand, they are generated from the header file (`src/ffi_gen_tmp.h`) by [package:ffigen](https://pub.dev/packages/ffigen) and stored temporarly into `lib/flutter_soloud_bindings_ffi_TMP.dart`.
Generate the bindings by running `dart run ffigen`.
Since I had the needs to modify the generated `.dart` I used this flow:
- copy into `src/ffi_gen_tmp.h` the functions declarations to be generated
- the `lib/flutter_soloud_bindings_ffi_TMP.dart` is generated
- copy the code generated into `lib/flutter_soloud_bindings_ffi.dart`, but only the code relative to the new functions

I have forked [SoLoud](https://github.com/jarikomppa/soloud) repo and modified it with the latest [Miniaudio](https://github.com/mackron/miniaudio) audio backend which, used by default, is in the [new_miniaudio] branch of my [fork](https://github.com/alnitak/soloud).

#### Project structure

This plugin uses the following structure:

* `lib`: Contains the Dart code that defines the API of the plugin relative to all platforms.

* `src`: Contains the native source code. Linux, Android and Windows have their own CmakeFile.txt file in their own subdir to build the code into a dynamic library.

* `src/soloud`: contains the SoLoud sources of my fork

#### Debugging
I left the **.vscode** dir which provides the settings to debug native c++ on Linux and Windows. To debug on Android use Android Studio and open the project in ***example/android*** dir.
I don't know how to debug native code on mac and iOS.

#### Linux
If you notice some glitches, they are probably caused by PulseAudio. 
You can try to disable it withing the `linux/src.cmake`:
Search for `add_definitions(-DMA_NO_PULSEAUDIO)` and uncomment it (default).

#### Android

Since the default audio backend is `miniaudio`, it will choose which backend to use:
- AAudio with Android 8.0+
- else OpenSL|ES

#### Windows

SoLoud uses *Openmpt* through DLL, available from https://lib.openmpt.org/
If you wish to use it, install and then enable it in the 1st line of *windows/src.cmake*

***Openmpt*** is a module-playing engine, capable of replaying wide variety of multichannel music (669, amf, ams, dbm, digi, dmf, dsm, far, gdm, ice, imf, it, itp, j2b, m15, mdl, med, mid, mo3, mod, mptm, mt2, mtm, okt, plm, psm, ptm, s3m, stm, ult, umx, wow, xm). It also loads wav files, and may support wider support for wav files than the stand-alone wav audio source.

#### iOS
On simulator the Impeller engine doesn't work. You need to disable it by running:
`flutter run --no-enable-impeller`
I do not have a real device to try.

#### Web

I tried hard to make this working on web! :(
I have succesfully compiled the sources with emscripten. In the **web** dir,
there is a little script to automate the compiling using CmakeLists.txt file.
This will build **libflutter_soloud_web_plugin.wasm*** and ***libflutter_soloud_web_plugin.bc***.

First I tried with [wasm_interop](https://pub.dev/packages/wasm_interop) plugin. But had errors loading and initializing the Module.

I tried also [web_ffi](https://pub.dev/packages/web_ffi) but seems it has been discontinued because it supports the old `dart:ffi API 2.12.0` and cannot be used here.

## TODOs

Many things can still be done.

The FFT data doesn't look as my wishes. Some works on *Analyzer::calcFFT()* in `src/analyzer.cpp` has yet to be done:
|name|description|
|--------|-|
|![spectrum1](https://github.com/alnitak/flutter_soloud/blob/master/img/flutter_soloud_spectrum.png "flutter_soloud spectrum")|flutter_soloud spectrum|
|![spectrum2](https://github.com/alnitak/flutter_soloud/blob/master/img/audacity_spectrum.png "audacity spectrum")|audacity spectrum|

For now only a small portion of the possibilities given by SoLoud have been implemented. Look [here](https://solhsa.com/soloud/index.html).
- audio filters
- 3D audio
- fading
- noise and waveform generation

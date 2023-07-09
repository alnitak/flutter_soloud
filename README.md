# Flutter audio plugin using SoLoud library

Flutter audio plugin using SoLoud library and FFI

[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

## Getting Started

flutter_soloud plugin uses [SoLoud](https://github.com/jarikomppa/soloud) forked repo where [miniaudio](https://github.com/mackron/miniaudio) audio backend has been updated.

So it is mandatory to clone this repo using:
```git clone --recursive git://github.com/alnitak/flutter_soloud.git```

If you already cloned normally, go into the repo dir and:
```git submodule update --init --recursive```

## Usage
First of all, SoLoudController must be initialized:
```
SoLoudController soLoudController = SoLoudController();

void main() {
  soLoudController.initialize();
  runApp(const MyApp());
}
```
The `soLoudController` will set the `soLoudFFI` which provides:
|function|returns|params|description|
|---|---|---|-----------------------|
|***initEngine***|PlayerErrors|-|Initialize the player. Must be called before any other player functions|
|***dispose***|-|-|Must be called when there is no more need of the player or when closing the app|
|***playFile***|PlayerErrors|`String` fileName|Play a new file|
|***speechText***|PlayerErrors|`String` textToSpeech|Speech the text given|
|***setVisualizationEnabled***|-|`bool` enabled|Enable or disable getting data from `getFft`, `getWave`, `getAudioTexture*`|
|***getFft***|-|`Pointer<Float>` fft|Returns a 256 float array containing FFT data.|
|***getWave***|-|`Pointer<Float>` wave|Returns a 256 float array containing wave data (magnitudes).|
|***getAudioTexture***|`Pointer<Float>` samples|-|Returns in `samples` a 512 float array.<br/>- The first 256 floats represent the FFT frequencies data [0.0~1.0].<br/>- The other 256 floats represent the wave data (amplitude) [-1.0~1.0].|
|***getAudioTexture2D***|-|`Pointer<Pointer<Float>>` samples|Return a floats matrix of 256x512.<br/>Every row are composed of 256 FFT values plus 256 wave data.<br/>Every time is called, a new row is stored in the first row and all the previous rows are shifted up (the last will be lost).|
|***getLength***|`double` time|-||
|***seek***|PlayerErrors|`double` time||
|***getPosition***|`double` time|-||
|***setFftSmoothing***|-|`double` smooth||

The `PlayerErrors` enum:
|name|description|
|---|---|---|
|***noError***|No error|
|***invalidParameter***|Some parameter is invalid|
|***fileNotFound***|File not found|
|***fileLoadFailed***|File found, but could not be loaded|
|***dllNotFound***|DLL not found, or wrong DLL|
|***outOfMemory***|Out of memory|
|***notImplemented***|Feature not implemented|
|***unknownError***|Other error|
|***backendNotInited***|Player not initialized|

## Contribute

To use the native code, bindings c/c++ to Dart is needed.
To avoid writing these by hand, they are generated from the header file (`src/ffi_gen_tmp.h`) by `package:ffigen` and stored into `lib/flutter_soloud_bindings_ffi_TMP.dart`.
Regenerate the bindings by running `dart run ffigen`.
Since I had the needs to modify the generated `.dart` I used this flow:
- copy into `src/ffi_gen_tmp.h` the declaration functions to be generated
- the `lib/flutter_soloud_bindings_ffi_TMP.dart` is generated
- copy the code generated into `lib/flutter_soloud_bindings_ffi.dart`, but only the code relative to the new functions

I have forked [SoLoud](https://github.com/jarikomppa/soloud) repo and modified it with the latest [Miniaudio](https://github.com/mackron/miniaudio) audio backend which, used by default, is in the [new_miniaudio] branch of my [fork](https://github.com/alnitak/soloud).

#### Project structure

This plugin uses the following structure:

* `src`: Contains the native source code. Linux, Android and Windows have their own CmakeFile.txt file in their own subdir for building that source code into a dynamic library.

* `lib`: Contains the Dart code that defines the API of the plugin, and which
  calls into the native code using `dart:ffi`.

* platform folders (`android`, `ios`, `windows`, etc.): Contains the build files for building and bundling the native code library with the platform application.

#### Debugging
I left the **.vscode** dir which provides the settings to debug native c++ on Linux and Windows. To debug on Android use Android Studio and open the project in ***example/android*** dir.

#### Linux
If you notice some glitches, are probably caused by PulseAudio. 
You can try to disable it withing the `linux/src.cmake`:
Search for `add_definitions(-DMA_NO_PULSEAUDIO)` and uncomment it.

#### Android

Since the default audio backend is `miniaudio`, it will choose which backend to use:
- AAudio with Android 8.0+
- else OpenSL|ES

#### Windows

SoLoud uses *Openmpt* through DLL, available from https://lib.openmpt.org/
If you wish to use it, install and then enable it in the 1st line of *windows/src.cmake*

***Openmpt*** is a module-playing engine, capable of replaying wide variety of multichannel music (669, amf, ams, dbm, digi, dmf, dsm, far, gdm, ice, imf, it, itp, j2b, m15, mdl, med, mid, mo3, mod, mptm, mt2, mtm, okt, plm, psm, ptm, s3m, stm, ult, umx, wow, xm). It also loads wav files, and may support wider support for wav files than the stand-alone wav audio source.

#### iOS
On simulator the Impeller engine doesn't work. You need to disable it when running:
`flutter run --no-enable-impeller`
I do not have a real device to try.

#### Web

I tried hard to make this working on web! :(
I have succesfully compiled the sources with emscripten. In the **web** dir,
there is a little script to automatize the compiling using CmakeLists.txt file.
This will build **libflutter_soloud_web_plugin.wasm*** and ***libflutter_soloud_web_plugin.bc***.

First I tried with [wasm_interop](https://pub.dev/packages/wasm_interop) plugin. But had errors loading and initializing the Module.

I tried also [web_ffi](https://pub.dev/packages/web_ffi) but seems it has been discontinued because it supports the old `dart:ffi API 2.12.0` and cannot be used here.
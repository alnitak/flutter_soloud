A low-level audio plugin for Flutter.

[![Pub Version](https://img.shields.io/pub/v/flutter_soloud?logo=dart)](https://pub.dev/packages/flutter_soloud)
[![style: very good analysis](https://img.shields.io/badge/style-very_good_analysis-B22C89.svg)](https://pub.dev/packages/very_good_analysis)

|Linux|Windows|Android|MacOS|iOS|web|
|:-:|:-:|:-:|:-:|:-:|:-:|
|💙|💙|💙|💙|💙|💙|

### Select features:

* Low latency, high performance
* Gapless looping (for background music, ambience, sound effects)
* Ability to load sounds to RAM, or play from disk
* Multiple voices, playing different or even the same sound 
  multiple times on top of each other
* Faders for attributes
  (e.g. fade out for 2 seconds, then stop)
* 3D positional audio, including Doppler effect
* Support for MP3, WAV, OGG, and FLAC
* Audio effects such as echo, reverb, filter, equalizer
* Web support is still under testing. Your feedback is greatly appreciated. Please read the [web notes](https://github.com/alnitak/flutter_soloud/blob/main/WEB_NOTES.md) to start using this plugin on the Web.


## Overview

This plugin is mainly meant for games and immersive apps.
If you merely need to play audio (such as playing a single sound effect
or a non-looped song), and you don't need to worry about latency,
there are other Flutter plugins you can use, such as the popular
[`audioplayers` plugin](https://pub.dev/packages/audioplayers).

> SoLoud is an easy to use, free, portable c/c++ audio engine for games.
> 
> The engine has been designed to make simple things easy, 
> while not making harder things impossible.
> 
> [(from the underlying engine's homepage)](https://solhsa.com/soloud/index.html)

The `flutter_soloud` plugin uses the 
[SoLoud (C++) audio engine](https://solhsa.com/soloud/)
with the [miniaudio](https://miniaud.io/) backend
through [Dart's C interop](https://dart.dev/interop/c-interop) (`dart:ffi`).
In other words, it is calling the C/C++ methods of the underlying audio engine
directly — there are no method channels in use.
To use this plugin on the **Web platform**, please refer to [WEB_NOTES](https://github.com/alnitak/flutter_soloud/blob/main/WEB_NOTES.md).


## Example

The following example loads an MP3 asset,
plays it, then later stops it 
and disposes of the audio source to reclaim memory.

```dart
void example() async {
  final soloud = SoLoud.instance;

  await soloud.init();

  final source = await soloud.loadAsset('path/to/asset.mp3');
  final handle = await soloud.play(source);

  // ...

  await soloud.stop(handle);
  await soloud.disposeSource(source);  
}
```

As you can see, most functionality in `flutter_soloud` is done through
calling methods on the `SoLoud` instance.
[**Read the API reference**](https://pub.dev/documentation/flutter_soloud/latest/flutter_soloud/SoLoud-class.html) 
for the full listing of methods, and their documentation. 

When you call a `loadAsset` (or `loadFile` or `loadUrl`) method, 
in return you get an `AudioSource`. This is the reference to the sound 
which is used by SoLoud. 
The source needs to be disposed when it is needed no more. 

Every time you play an `AudioSource`, you get a new `SoundHandle`
which uniquely identifies the new playing instance of the sound.
This handle is also added to the `AudioSource.handles` list so that you can
always check how many times any audio source is being played at the time.

The `SoundHandle` also allows you to modify the currently-playing sounds,
such as changing their volume, pausing them, etc.

For more examples, check out the 
[`example/` project](https://github.com/alnitak/flutter_soloud/tree/main/example)
included with the package.


## Logging

The `flutter_soloud` package logs everything
(from severe warnings to fine debug messages) using the standard 
[`logging` package](https://pub.dev/packages/logging).

See the example's `lib/main.dart` to see how to capture these logs.
For example:

```dart
import 'dart:developer' as dev;

void main() {
  // Cut-off for messages. (Lower levels than INFO will be discarded.)
  Logger.root.level = Level.FINE;
  Logger.root.onRecord.listen((record) {
    // Forward logs to the console.
    dev.log(
      record.message,
      time: record.time,
      level: record.level.value,
      name: record.loggerName,
      zone: record.zone,
      error: record.error,
      stackTrace: record.stackTrace,
    );
    // TODO: if needed, forward to Sentry.io, Crashlytics, etc.
  });

  runApp(const MyApp());
}
```

If you don't set up a listener like the one above, there will be no logging
from the package.

See the `logging` package's [documentation](https://pub.dev/packages/logging)
to learn more about its functionality.


## License

The Dart plugin is covered by the MIT license.

For information regarding the license for the underlying SoLoud (C++) engine, 
please refer to [this link](https://solhsa.com/soloud/legal.html).
In short, the SoLoud code itself is covered by
the ZLib/LibPNG license
(which is [compatible](https://en.wikipedia.org/wiki/Zlib_License) with GNU GPL).
Some modules (such as MP3 or OGG support) are covered with other, but still
permissive open source licenses.


## Contribute

To use native code, bindings from Dart to C/C++ are needed. To avoid writing these manually, they are generated from the header file (`src/ffi_gen_tmp.h`) using [package:ffigen](https://pub.dev/packages/ffigen) and temporarily stored in `lib/flutter_soloud_FFIGEN.dart`. You can generate the bindings by running `dart run ffigen`.

Since I needed to modify the generated `.dart` file, I followed this flow:
1. Copy the function declarations to be generated into `src/ffi_gen_tmp.h`.
2. The file `lib/flutter_soloud_FFIGEN.dart` will be generated.
3. Copy the relevant code for the new functions from `lib/flutter_soloud_FFIGEN.dart` into `lib/flutter_soloud_bindings_ffi.dart`.

#### Project structure

This plugin uses the following structure:

* `lib`: Contains the Dart code that defines the API of the plugin relative to all platforms.

* `src`: Contains the native source code. Linux, Android and Windows have their own CmakeFile.txt file in their own subdir to build the code into a dynamic library.

* `src/soloud`: contains the SoLoud sources of my fork

The `flutter_soloud` plugin utilizes a [forked](https://github.com/alnitak/soloud)
repository of [SoLoud](https://github.com/jarikomppa/soloud),
where the [miniaudio](https://github.com/mackron/miniaudio) audio backend (used by default) has been updated and
it is located in `src/soloud/src/backend/miniaudio`.

#### Debugging

I have provided the necessary settings in the **.vscode** directory for debugging native C++ code on both Linux and Windows. To debug on Android, please use Android Studio and open the project located in the ***example/android*** directory. However, I am not familiar with the process of debugging native code on Mac and iOS.

##### Logging

When debugging the package using the `example/` app, you might want to change
the logging level to something more granular. For example, in `main()`:

```dart
// Capture even the finest log messages.
Logger.root.level = Level.ALL;
```

One thing that's missing (as of March 2024) is logging 
from inside the audio isolate. 
We'd have to send logs to the main isolate through an event, 
which might be too expensive and brittle. 
Feel free to use `debugPrint` in `audio_isolate.dart` 
when working on the package. 
Just make sure to delete those calls before submitting pull requests.
We don't want to pollute developers' console outputs.

#### Linux

If you encounter any glitches, they might be caused by PulseAudio. To troubleshoot this issue, you can try disabling PulseAudio within the `linux/src.cmake` file. Look for the line `add_definitions(-DMA_NO_PULSEAUDIO)` and uncomment it (now it is the default behavior).

#### Android

The default audio backend is `miniaudio`, which will automatically select the appropriate audio backend based on your Android version:
- AAudio with Android 8.0 and newer.
- OpenSL|ES for older Android versions.

#### Windows

For Windows users, SoLoud utilizes *Openmpt* through a DLL, which can be obtained from [https://lib.openmpt.org/](https://lib.openmpt.org/). If you wish to use this feature, install the DLL and enable it by modifying the first line in `windows/src.cmake`.

***Openmpt*** functions as a module-playing engine, capable of replaying a wide variety of multichannel music formats (669, amf, ams, dbm, digi, dmf, dsm, far, gdm, ice, imf, it, itp, j2b, m15, mdl, med, mid, mo3, mod, mptm, mt2, mtm, okt, plm, psm, ptm, s3m, stm, ult, umx, wow, xm). Additionally, it can load wav files and may offer better support for wav files compared to the stand-alone wav audio source.

#### Web

Work on web support (using WASM) is tracked in
https://github.com/alnitak/flutter_soloud/issues/46.


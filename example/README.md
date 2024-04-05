# flutter_soloud_example

Demonstrates how to use the `flutter_soloud` plugin.

There are some problems with Impeller engine in iOS when running the *visualizer* example (the 2nd) on the simulator (20 Lug 2023). To disable it, run the following command:
`flutter run --no-enable-impeller`.

There are 5 examples:
*(to use microphone on MacOs or iOS you should add audio input permission in the example app)*

**The 1st** is a simple use-case to show how to play a sound and how to activate the capture.

**The 2nd** aims to show a visualization of frequencies and wave data.
The file [**Visualizer.dart**] uses `getAudioTexture2D` to store new audio data into `audioData` on every tick.

The video below illustrates how the data is then converted to an image (the upper widget) and sent to the shader (the middle widget).
The bottom widgets use FFT data on the left and wave data represented with a row of yellow vertical containers with the height taken from `audioData` on the right.

The `getAudioTexture2D` returns an array of 512x256. Each row contains 256 Floats of FFT data and 256 Floats of wave data, making it possible to write a shader like a spectrogram (shader #8) or a 3D visualization (shader #9).

Shaders from 1 to 7 are using just 1 row of the `audioData`. Therefore, the texture generated to feed the shader should be 256x2 px. The 1st row represents the FFT data, and the 2nd represents the wave data.

Since many operations are required for each frame, the CPU and GPU can be under stress, leading to overheating of a mobile device.
It seems that sending an image (with `setImageSampler()`) to the shader is very expensive. You can observe this by disabling the shader widget.

https://github.com/alnitak/flutter_soloud/assets/192827/384c88aa-5daf-4f10-a879-169ab8522690


***The 3rd*** example demonstrates how to manage sounds using their handles: every sound should be loaded before it can be played. Loading a sound can take some time and should not be done during gameplay, for instance, in a game. Once a sound is loaded, it can be played, and every instance of that same audio will be identified by its *handle*.

The example shows how you can have background music and play a fire sound multiple times.

https://github.com/alnitak/flutter_soloud/assets/192827/92c9db80-80ee-4a27-b6a9-3e089ffe600e


***The 4th*** example shows how to enance audio with 3D capabilities. There is a circle where the listener is placed in the center and a moving siren audio is represented by a little circle which is automatically animated or can be moved by mouse gesture. The sound volume fades off at the circonference. There is also a doppler effect that can be turned off.

https://github.com/alnitak/flutter_soloud/assets/192827/f7cf9d71-be4f-4c83-99ff-89dbd9378859


***The 5th*** example shows how to generete [**AudioSource**] key sounds. There is a handy tool method to generate the 12 key notes of a given octave. A widget to play them can be used with the touch or a keyboard. Different types of waveforms can be chosen including square,`saw`,`sin`,`triangle`,`bounce`,`jaws`,`humps`,`fSquare` and `fSaw`.
There are also simple knobs to adjust faders and oscillators. Other knobs to add/remove audio effects.

https://github.com/alnitak/flutter_soloud/assets/192827/bfc5aa73-6dbc-42f5-90e4-bc1cc5e181e0

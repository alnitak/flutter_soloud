Opus and Ogg libraries are currently only used by the BufferStream to receive and fill audio data with Opus format and they are enabled by default.

These libs are prebuilt for all platforms and stored in the plugin folder. This means that even if they are not needed, the plugin still requires them to work, and the size of the app will grow slightly. To address this, a new environment variable has been introduced that can be used to prevent the prebuilt libs from being linked to the final app.

If trying to use, for example the `SoLoud.setBufferStream()` function, an exception will be thrown if the environment variable has been set.

---

### How to set the environment variable:

**Linux - Android - Windows**

If using **VS Code**, add `NO_OPUS_OGG_LIBS` set to an empty string in the `env` key to the *.vscode/launch.json* configuration, for example:

```
{
    "name": "Flutter debug",
    "type": "dart",
    "request": "launch",
    "program": "lib/buffer_stream/websocket.dart",
    "flutterMode": "debug",
    "env": {
        "NO_OPUS_OGG_LIBS": "1"
    },
    "cwd": "${workspaceFolder}/example"
}
```

If using **Android Studio**, add `NO_OPUS_OGG_LIBS="1"` in the *Run/Debug Configurations* configuration under **Environment Variables** text field.

Alternatively, you can set the variable in the terminal and build the app, for example:

```
flutter clean
flutter pub get
NO_OPUS_OGG_LIBS="1" && flutter run
```

**MacOS - iOS**

To set the environment variable on MacOS and iOS, you can add the following line to the `app/ios/Podfile` or `app/macos/Podfile` at the top of the file:

```
ENV['NO_OPUS_OGG_LIBS'] = '1'
```

Alternatively, you can set the variable in the terminal and build the app, for example:

```
flutter clean
flutter pub get
NO_OPUS_OGG_LIBS="1" && flutter run
```
if the environment variable was already been used before for testing purposes, you can use the following command:
```
flutter clean
flutter pub get
NO_OPUS_OGG_LIBS= && flutter run <-- this will unset the environment variable
```

> [!NOTE]  
> The environment variable set in VS Code or in Android Studio will be ignored.

**Web**

To set the environment variable on Web, open `web/compile_wasm.sh` and change the line `NO_OPUS_OGG_LIBS="0"` to `NO_OPUS_OGG_LIBS="1"`.
You should then run the script to build the WASM and JS files. You must have `emscripten` installed.

The script works on Linux and probably on MacOS. Windows users should run the script in WSL or wait until a *.bat* script is available.

> [!NOTE]  
> The environment variable set in VS Code or in Android Studio will be ignored.
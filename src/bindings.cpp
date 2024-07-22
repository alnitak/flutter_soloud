
#include "player.h"
#include "analyzer.h"
#include "synth/basic_wave.h"
#ifndef COMMON_H
#include "common.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include "soloud/include/soloud_fft.h"
#include "soloud_thread.h"

#include <stdio.h>
#include <iostream>
#include <memory.h>
#include <memory>

#ifdef __cplusplus
extern "C"
{
#endif

    std::unique_ptr<Player> player = nullptr;
    std::unique_ptr<Analyzer> analyzer = std::make_unique<Analyzer>(2048);

    typedef void (*dartVoiceEndedCallback_t)(unsigned int *);
    typedef void (*dartFileLoadedCallback_t)(enum PlayerErrors *, char *completeFileName, unsigned int *);
    typedef void (*dartStateChangedCallback_t)(enum PlayerStateEvents *);

    // to be used by `NativeCallable`, these functions must return void.
    void (*dartVoiceEndedCallback)(unsigned int *) = nullptr;
    void (*dartFileLoadedCallback)(enum PlayerErrors *, char *completeFileName, unsigned int *) = nullptr;
    void (*dartStateChangedCallback)(enum PlayerStateEvents *) = nullptr;

    //////////////////////////////////////////////////////////////
    /// WEB WORKER

#ifdef __EMSCRIPTEN__
    /// Create the web worker and store a global "Module.workerUri" in JS.
    FFI_PLUGIN_EXPORT void createWorkerInWasm()
    {
        printf("CPP void createWorkerInWasm()\n");

        EM_ASM({
            if (!Module.wasmWorker)
            {
                // Create a new Worker from the URI
                var workerUri = "assets/packages/flutter_soloud/web/worker.dart.js";
                console.log("EM_ASM creating web worker!");
                Module.wasmWorker = new Worker(workerUri);
            }
            else
            {
                console.log("EM_ASM web worker already created!");
            }
        });
    }

    /// Post a message with the web worker.
    FFI_PLUGIN_EXPORT void sendToWorker(const char *message, int value)
    {
        EM_ASM({
            if (Module.wasmWorker)
            {
                console.log("EM_ASM posting message " + UTF8ToString($0) + 
                    " with value " + $1);
                // Send the message
                Module.wasmWorker.postMessage(JSON.stringify({
                    "message" : UTF8ToString($0),
                    "value" : $1
                }));
            }
            else
            {
                console.error('Worker not found.');
            } }, message, value);
    }
#endif

    FFI_PLUGIN_EXPORT void nativeFree(void *pointer)
    {
        free(pointer);
    }

    /// The callback to monitor when a voice ends.
    ///
    /// It is called by void `Soloud::stopVoice_internal(unsigned int aVoice)` when a voice ends
    /// and comes from the audio thread (so on the web, from a different web worker).
    FFI_PLUGIN_EXPORT void voiceEndedCallback(unsigned int *handle)
    {
#ifdef __EMSCRIPTEN__
        // Calling JavaScript from C/C++
        // https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html#interacting-with-code-call-javascript-from-native
        // emscripten_run_script("voiceEndedCallbackJS('1234')");
        sendToWorker("voiceEndedCallback", *handle);
#endif

        if (dartVoiceEndedCallback == nullptr)
            return;
        player->removeHandle(*handle);
        // [n] pointer must be deleted on Dart.
        unsigned int *n = (unsigned int *)malloc(sizeof(unsigned int *));
        *n = *handle;
        dartVoiceEndedCallback(n);
    }

    /// The callback to monitor when a file is loaded.
    void fileLoadedCallback(enum PlayerErrors error, char *completeFileName, unsigned int *hash)
    {
        if (dartFileLoadedCallback == nullptr)
            return;
        // [e,name,n] pointers must be deleted on Dart.
        PlayerErrors *e = (PlayerErrors *)malloc(sizeof(PlayerErrors *));
        *e = error;
        char *name = strdup(completeFileName);
        unsigned int *n = (unsigned int *)malloc(sizeof(unsigned int *));
        *n = *hash;
        dartFileLoadedCallback(e, name, n);
    }

    void stateChangedCallback(unsigned int state)
    {
        PlayerStateEvents *type = (PlayerStateEvents *)malloc(sizeof(unsigned int *));
        *type = (PlayerStateEvents)state;
        if (dartStateChangedCallback != nullptr)
            dartStateChangedCallback(type);
    }

    /// Set a Dart functions to call when an event occurs.
    ///
    FFI_PLUGIN_EXPORT void setDartEventCallback(
        dartVoiceEndedCallback_t voice_ended_callback,
        dartFileLoadedCallback_t file_loaded_callback,
        dartStateChangedCallback_t state_changed_callback)
    {
        dartVoiceEndedCallback = voice_ended_callback;
        dartFileLoadedCallback = file_loaded_callback;
        dartStateChangedCallback = state_changed_callback;
    }

    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////////////

    /// Initialize the player. Must be called before any other player functions.
    ///
    /// [sampleRate] the sample rate. Usually is 22050, 44100 (CD quality) or 48000.
    /// [bufferSize] the audio buffer size. Usually is 2048, but can be also 512 when
    /// low latency is needed for example in games.
    /// [channels] 1=mono, 2=stereo, 4=quad, 6=5.1, 8=7.1.
    ///
    /// Returns [PlayerErrors.noError] if success.
    FFI_PLUGIN_EXPORT enum PlayerErrors initEngine(
        unsigned int sampleRate,
        unsigned int bufferSize,
        unsigned int channels)
    {
        if (player.get() == nullptr)
            player = std::make_unique<Player>();

        player.get()->setStateChangedCallback(stateChangedCallback);
        PlayerErrors res = (PlayerErrors)player.get()->init(sampleRate, bufferSize, channels);
        if (res != noError)
            return res;

        // Set window size for filters
        const int windowSize = (player.get()->soloud.getBackendBufferSize() /
                                player.get()->soloud.getBackendChannels()) -
                               1;
        analyzer.get()->setWindowsSize(windowSize);

        // Set the callback for when a voice is ended/stopped
        player.get()->setVoiceEndedCallback(voiceEndedCallback);

        return (PlayerErrors)noError;
    }

    /// Must be called when there is no more need of the player or when closing the app
    ///
    FFI_PLUGIN_EXPORT void dispose()
    {
        if (player.get() == nullptr)
            return;
        dartVoiceEndedCallback = nullptr;
        player.get()->dispose();
        player = nullptr;
    }

    FFI_PLUGIN_EXPORT int isInited()
    {
        if (player.get() == nullptr)
            return 0;
        return player.get()->isInited() ? 1 : 0;
    }

    /// Load a new sound to be played once or multiple times later.
    ///
    /// After loading the file, the [fileLoadedCallback] will call the
    /// Dart function defined with [setDartEventCallback] which gives back
    /// the error and the new hash.
    ///
    /// [completeFileName] the complete file path.
    /// [loadIntoMem] if true Soloud::wav will be used which loads
    /// all audio data into memory. This will be useful when
    /// the audio is short, ie for game sounds, mainly used to prevent
    /// gaps or lags when starting a sound (less CPU, more memory allocated).
    /// If false, Soloud::wavStream will be used and the audio data is loaded
    /// from the given file when needed (more CPU, less memory allocated).
    /// See the [seek] note problem when using [loadIntoMem] = false
    /// [hash] return the hash of the sound
    /// Returns [PlayerErrors.noError] if success
    FFI_PLUGIN_EXPORT void loadFile(
        char *completeFileName,
        bool loadIntoMem)
    {
        // this check is already been done in Dart
        if (player.get() == nullptr || !player.get()->isInited())
        {
            printf("WARNING (from SoLoud C++ binding code): the player has "
                   "not yet been initialized. This is likely a bug in flutter_soloud. "
                   "Please report the bug.");
            return;
        }

        Player *p = player.get();
        unsigned int hash = 0;
        // std::thread loadThread([p, completeFileName, loadIntoMem, hash]()
        //                        {
        PlayerErrors error = p->loadFile(completeFileName, loadIntoMem, &hash);
        // printf("*** LOAD FILE FROM THREAD error: %d  hash: %u\n", error,  *hash);
        fileLoadedCallback(error, completeFileName, &hash);
        //     });
        // // TODO(marco): use .detach()? Use std::atomic somewhere
        // loadThread.join();
    }

    /// Load a new sound stored into [buffer] to be played once or multiple times later.
    /// Mainly used on web because the browsers are not allowed to read files directly.
    ///
    /// [uniqueName] the unique name of the sound. Used only to have the [hash].
    /// [buffer] the audio data. These contains the audio file bytes.
    /// [length] the length of [buffer].
    /// [loadIntoMem] if true Soloud::wav will be used which loads
    /// all audio data into memory. This will be useful when
    /// the audio is short, ie for game sounds, mainly used to prevent
    /// gaps or lags when starting a sound (less CPU, more memory allocated).
    /// If false, Soloud::wavStream will be used and the audio data is loaded
    /// from the given file when needed (more CPU, less memory allocated).
    /// See the [seek] note problem when using [loadIntoMem] = false
    /// [hash] return the hash of the sound.
    FFI_PLUGIN_EXPORT enum PlayerErrors loadMem(
        char *uniqueName,
        unsigned char *buffer,
        int length,
        int loadIntoMem,
        unsigned int *hash)
    {
        // this check is already been done in Dart
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        return (PlayerErrors)player.get()->loadMem(uniqueName, buffer, length, loadIntoMem, *hash);
    }

    /// Load a new waveform to be played once or multiple times later
    ///
    /// [waveform]  WAVE_SQUARE = 0,
    ///             WAVE_SAW,
    ///             WAVE_SIN,
    ///             WAVE_TRIANGLE,
    ///             WAVE_BOUNCE,
    ///             WAVE_JAWS,
    ///             WAVE_HUMPS,
    ///             WAVE_FSQUARE,
    ///             WAVE_FSAW
    /// [superWave]
    /// [scale]
    /// [detune]
    /// [hash] return hash of the sound
    /// Returns [PlayerErrors.noError] if success
    FFI_PLUGIN_EXPORT enum PlayerErrors loadWaveform(
        int waveform,
        bool superWave,
        float scale,
        float detune,
        unsigned int *hash)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        return (PlayerErrors)player.get()->loadWaveform(waveform, superWave, scale, detune, *hash);
    }

    /// Set the scale of an already loaded waveform identified by [hash]
    ///
    /// [hash] the unique sound hash of a waveform sound
    /// [newScale]
    FFI_PLUGIN_EXPORT void setWaveformScale(unsigned int hash, float newScale)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;

        player.get()->setWaveformScale(hash, newScale);
    }

    /// Set the detune of an already loaded waveform identified by [hash]
    ///
    /// [hash] the unique sound hash of a waveform sound
    /// [newDetune]
    FFI_PLUGIN_EXPORT void setWaveformDetune(unsigned int hash, float newDetune)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;

        player.get()->setWaveformDetune(hash, newDetune);
    }

    /// Set a new frequency of an already loaded waveform identified by [hash]
    ///
    /// [hash] the unique sound hash of a waveform sound
    /// [newFreq]
    FFI_PLUGIN_EXPORT void setWaveformFreq(unsigned int hash, float newFreq)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;

        player.get()->setWaveformFreq(hash, newFreq);
    }

    /// Set a new frequence of an already loaded waveform identified by [hash]
    ///
    /// [hash] the unique sound hash of a waveform sound
    /// [superwave]
    FFI_PLUGIN_EXPORT void setSuperWave(unsigned int hash, bool superwave)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;

        player.get()->setWaveformSuperwave(hash, superwave);
    }

    /// Set a new wave form of an already loaded waveform identified by [hash]
    ///
    /// [hash] the unique sound hash of a waveform sound
    /// [newWaveform]   WAVE_SQUARE = 0,
    ///                 WAVE_SAW,
    ///                 WAVE_SIN,
    ///                 WAVE_TRIANGLE,
    ///                 WAVE_BOUNCE,
    ///                 WAVE_JAWS,
    ///                 WAVE_HUMPS,
    ///                 WAVE_FSQUARE,
    ///                 WAVE_FSAW
    FFI_PLUGIN_EXPORT void setWaveform(unsigned int hash, int newWaveform)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;

        player.get()->setWaveform(hash, newWaveform);
    }

    /// Speech the text given
    ///
    /// [textToSpeech]
    /// Returns [PlayerErrors.noError] if success and [handle] sound identifier
    /// TODO(marco): add other T2S parameters
    FFI_PLUGIN_EXPORT enum PlayerErrors speechText(char *textToSpeech, unsigned int *handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        return (PlayerErrors)player.get()->textToSpeech(textToSpeech, *handle);
    }

    /// Switch pause state for an already loaded sound identified by [handle]
    ///
    /// [handle] the sound handle
    FFI_PLUGIN_EXPORT void pauseSwitch(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->pauseSwitch(handle);
    }

    /// Pause or unpause already loaded sound identified by [handle]
    ///
    /// [handle] the sound handle
    /// [pause] the sound handle
    FFI_PLUGIN_EXPORT void setPause(unsigned int handle, bool pause)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->setPause(handle, pause);
    }

    /// Gets the pause state
    ///
    /// [handle] the sound handle
    /// Return true if paused
    FFI_PLUGIN_EXPORT int getPause(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return false;
        return player.get()->getPause(handle) ? 1 : 0;
    }

    /// Set a sound's relative play speed.
    /// Setting the value to 0 will cause undefined behavior, likely a crash.
    /// Change the relative play speed of a sample. This changes the effective
    /// sample rate while leaving the base sample rate alone.
    ///
    /// Note that playing a sound at a higher sample rate will require SoLoud
    /// to request more samples from the sound source, which will require more
    /// memory and more processing power. Playing at a slower sample rate
    /// is cheaper.
    ///
    /// [handle] the sound handle
    /// [speed] the new speed
    FFI_PLUGIN_EXPORT void setRelativePlaySpeed(unsigned int handle, float speed)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->setRelativePlaySpeed(handle, speed);
    }

    /// Get a sound's relative play speed.
    /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.

    /// Return the current play speed.
    /// [handle] the sound handle
    FFI_PLUGIN_EXPORT float getRelativePlaySpeed(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return 1;
        return player.get()->getRelativePlaySpeed(handle);
    }

    /// Play already loaded sound identified by [hash]
    ///
    /// [hash] the unique sound hash of a sound
    /// [volume] 1.0f full volume
    /// [pan] 0.0f centered
    /// [paused] 0 not paused
    /// [newHandle] pointer to the handle for this new sound
    /// [looping] whether to start the sound in looping state.
    /// [loopingStartAt] If looping is enabled, the loop point is, by default,
    /// the start of the stream. The loop start point can be set with this parameter, and
    /// current loop point can be queried with [getLoopingPoint] and
    /// changed by [setLoopingPoint].
    /// Return the error if any and a new [handle] of this sound
    FFI_PLUGIN_EXPORT enum PlayerErrors play(
        unsigned int soundHash,
        float volume,
        float pan,
        bool paused,
        bool looping,
        double loopingStartAt,
        unsigned int *handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        unsigned int newHandle = player.get()->play(soundHash, volume, pan, paused, looping, loopingStartAt);
        *handle = newHandle;
        return *handle == 0 ? soundHashNotFound : noError;
    }

    /// Stop already loaded sound identified by [handle] and clear it
    ///
    /// [handle]
    FFI_PLUGIN_EXPORT void stop(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->stop(handle);
    }

    /// Stop all handles of the already loaded sound identified by [hash] and dispose it
    ///
    /// [soundHash]
    FFI_PLUGIN_EXPORT void disposeSound(unsigned int soundHash)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->disposeSound(soundHash);
    }

    /// Dispose all sounds already loaded
    ///
    FFI_PLUGIN_EXPORT void disposeAllSound()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->disposeAllSound();
    }

    /// Query whether a sound is set to loop.
    ///
    /// [handle]
    /// Returns true if flagged for looping.
    FFI_PLUGIN_EXPORT int getLooping(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return 0;
        return player.get()->getLooping(handle) == 1;
    }

    /// This function can be used to set a sample to play on repeat,
    /// instead of just playing once
    ///
    /// [soundHash]
    /// [enable]
    FFI_PLUGIN_EXPORT void setLooping(unsigned int handle, bool enable)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->setLooping(handle, enable);
    }

    /// Get sound loop point value.
    ///
    /// [handle]
    /// Returns the time in seconds.
    FFI_PLUGIN_EXPORT double getLoopPoint(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return 0;
        return player.get()->getLoopPoint(handle);
    }

    /// Set sound loop point value.
    ///
    /// [handle]
    /// [time] in seconds.
    FFI_PLUGIN_EXPORT void setLoopPoint(unsigned int handle, double time)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->setLoopPoint(handle, time);
    }

    /// Enable or disable visualization
    ///
    /// [enabled] enable or disable it
    FFI_PLUGIN_EXPORT void setVisualizationEnabled(bool enabled)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->setVisualizationEnabled(enabled);
    }

    /// Get visualization state
    ///
    /// Return true if enabled
    FFI_PLUGIN_EXPORT int getVisualizationEnabled()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0;
        return player.get()->isVisualizationEnabled();
    }

    /// Returns valid data only if VisualizationEnabled is true
    ///
    /// Return a 256 float array containing FFT data.
    FFI_PLUGIN_EXPORT void getFft(float **fft)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        *fft = player.get()->calcFFT();
    }

    /// Returns valid data only if VisualizationEnabled is true
    ///
    /// Return a 256 float array containing wave data.
    FFI_PLUGIN_EXPORT void getWave(float **wave)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        *wave = player.get()->getWave();
    }

    /// Smooth FFT data.
    /// When new data is read and the values are decreasing, the new value will be
    /// decreased with an amplitude between the old and the new value.
    /// This will result on a less shaky visualization.
    ///
    /// [smooth] must be in the [0.0 ~ 1.0] range.
    /// 0 = no smooth
    /// 1 = full smooth
    /// the new value is calculated with:
    /// newFreq = smooth * oldFreq + (1 - smooth) * newFreq
    FFI_PLUGIN_EXPORT void setFftSmoothing(float smooth)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        analyzer.get()->setSmoothing(smooth);
    }

    /// Return in [samples] a 512 float array.
    /// The first 256 floats represent the FFT frequencies data [>=0.0].
    /// The other 256 floats represent the wave data (amplitude) [-1.0~1.0].
    ///
    /// [samples] should be allocated and freed in dart side
    FFI_PLUGIN_EXPORT void getAudioTexture(float *samples)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            analyzer.get() == nullptr)
        {
            memset(samples, 0, sizeof(float) * 512);
            return;
        }
        float *wave = player.get()->getWave();
        float *fft = analyzer.get()->calcFFT(wave);

        memcpy(samples, fft, sizeof(float) * 256);
        memcpy(samples + 256, wave, sizeof(float) * 256);
    }

    /// Return a floats matrix of 256x512
    /// Every row are composed of 256 FFT values plus 256 of wave data
    /// Every time is called, a new row is stored in the
    /// first row and all the previous rows are shifted
    /// up (the last one will be lost).
    ///
    /// [samples]
    float texture2D[256][512];
    FFI_PLUGIN_EXPORT enum PlayerErrors getAudioTexture2D(float **samples)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            analyzer.get() == nullptr || !player.get()->isVisualizationEnabled())
        {
            *samples = *texture2D;
            memset(*samples, 0, sizeof(float) * 512 * 256);
            return backendNotInited;
        }
        /// shift up 1 row
        memmove(*texture2D + 512, texture2D, sizeof(float) * 512 * 255);
        /// store the new 1st row
        getAudioTexture(texture2D[0]);
        *samples = *texture2D;
        return noError;
    }

    FFI_PLUGIN_EXPORT float getTextureValue(int row, int column)
    {
        return texture2D[row][column];
    }

    /// Get the sound length in seconds
    ///
    /// [soundHash] the sound hash
    /// Returns sound length in seconds
    FFI_PLUGIN_EXPORT double getLength(unsigned int soundHash)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0.0;
        auto f = player.get()->getLength(soundHash);
        return f;
    }

    /// Seek playing in [time] seconds
    /// [time]
    /// [handle] the sound handle
    /// Returns [PlayerErrors.noError] if success
    ///
    /// NOTE: when seeking an mp3 file loaded using `loadIntoMem`=false
    /// the seek operation is not performed due to lags. This occurs because the
    /// mp3 codec must compute each frame length to gain a new position.
    /// The problem is explained in souloud_wavstream.cpp
    /// in `WavStreamInstance::seek` function.
    ///
    /// This mode is useful ie for background music, not for a music player
    /// where a seek slider for mp3s is a must.
    /// If you need seeking mp3, please, use `loadIntoMem`=true instead
    /// or other audio formats!
    ///
    FFI_PLUGIN_EXPORT enum PlayerErrors seek(unsigned int handle, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        return (PlayerErrors)player.get()->seek(handle, time);
    }

    /// Get current sound position  in seconds
    ///
    /// [handle] the sound handle
    /// Returns time in seconds
    FFI_PLUGIN_EXPORT double getPosition(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return 0.0;
        return player.get()->getPosition(handle);
    }

    /// Get current Global volume
    ///
    /// Returns the volume
    FFI_PLUGIN_EXPORT double getGlobalVolume()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0.0;
        return player.get()->getGlobalVolume();
    }

    /// Set current Global volume
    ///
    /// Returns the volume
    FFI_PLUGIN_EXPORT enum PlayerErrors setGlobalVolume(float volume)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->setGlobalVolume(volume);
        return noError;
    }

    /// Get current [handle] volume
    ///
    /// Returns the volume
    FFI_PLUGIN_EXPORT double getVolume(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return 0.0;
        return player.get()->getVolume(handle);
    }

    /// Set current [handle] volume
    ///
    FFI_PLUGIN_EXPORT enum PlayerErrors setVolume(unsigned int handle, float volume)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        if (!player.get()->isValidHandle(handle))
            return soundHandleNotFound;
        player.get()->setVolume(handle, volume);
        return noError;
    }

    /// Get a sound's current pan setting.
    ///
    /// [handle] the sound handle.
    /// Returns the range of the pan values is -1 to 1, where -1 is left, 0 is middle and and 1 is right.
    FFI_PLUGIN_EXPORT double getPan(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0.0f;

        return player.get()->getPan(handle);
    }

    /// Set a sound's current pan setting.
    ///
    /// [handle] the sound handle.
    /// [pan] the range of the pan values is -1 to 1, where -1 is left, 0 is middle and and 1 is right.
    FFI_PLUGIN_EXPORT void setPan(unsigned int handle, double pan)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        // Rounding to 6 decimal to work around the float to double precision.
        player.get()->setPan(handle, pan);
    }

    /// Set the left/right volumes directly.
    /// Note that this does not affect the value returned by getPan.
    ///
    /// [handle] the sound handle.
    /// [panLeft] value for the left pan.
    /// [panRight] value for the right pan.
    FFI_PLUGIN_EXPORT void setPanAbsolute(unsigned int handle, double panLeft, double panRight)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->setPanAbsolute(handle, panLeft, panRight);
    }

    /// Check if a handle is still valid.
    ///
    /// [handle] handle to check
    /// Return true if it still exists
    FFI_PLUGIN_EXPORT int getIsValidVoiceHandle(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return false;
        return player.get()->isValidHandle(handle) ? 1 : 0;
    }

    /// Returns the number of concurrent sounds that are playing at the moment.
    FFI_PLUGIN_EXPORT unsigned int getActiveVoiceCount()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0;
        return player.get()->getActiveVoiceCount();
    }

    /// Returns the number of concurrent sounds that are playing a specific audio source.
    FFI_PLUGIN_EXPORT int countAudioSource(unsigned int soundHash)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0;
        return player.get()->countAudioSource(soundHash);
    }

    /// Returns the number of voices the application has told SoLoud to play.
    FFI_PLUGIN_EXPORT unsigned int getVoiceCount()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0;
        return player.get()->getVoiceCount();
    }

    /// Get a sound's protection state.
    FFI_PLUGIN_EXPORT bool getProtectVoice(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return false;
        return player.get()->getProtectVoice(handle);
    }

    /// Set a sound's protection state.
    ///
    /// Normally, if you try to play more sounds than there are voices,
    /// SoLoud will kill off the oldest playing sound to make room.
    /// This will most likely be your background music. This can be worked
    /// around by protecting the sound.
    /// If all voices are protected, the result will be undefined.
    ///
    /// [handle]  handle to check.
    /// [protect] whether to protect or not.
    FFI_PLUGIN_EXPORT void setProtectVoice(unsigned int handle, bool protect)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            !player.get()->isValidHandle(handle))
            return;
        player.get()->setProtectVoice(handle, protect);
    }

    /// Get the current maximum active voice count.
    FFI_PLUGIN_EXPORT unsigned int getMaxActiveVoiceCount()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return 0;
        return player.get()->getMaxActiveVoiceCount();
    }

    /// Set the current maximum active voice count.
    /// If voice count is higher than the maximum active voice count,
    /// SoLoud will pick the ones with the highest volume to actually play.
    /// [maxVoiceCount] the max concurrent sounds that can be played.
    ///
    /// NOTE: The number of concurrent voices is limited, as having unlimited
    /// voices would cause performance issues, as well as lead to unnecessary clipping.
    /// The default number of concurrent voices is 16, but this can be adjusted at runtime.
    /// The hard maximum number is 4095, but if more are required, SoLoud can be modified to
    /// support more. But seriously, if you need more than 4095 sounds at once, you're
    /// probably going to make some serious changes in any case.
    FFI_PLUGIN_EXPORT void setMaxActiveVoiceCount(unsigned int maxVoiceCount)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->setMaxActiveVoiceCount(maxVoiceCount);
    }

    /////////////////////////////////////////
    /// voice groups
    /////////////////////////////////////////

    /// Used to create a new voice group. Returns 0 if not successful.
    FFI_PLUGIN_EXPORT unsigned int createVoiceGroup()
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return -1;
        auto ret = player.get()->createVoiceGroup();
        return ret;
    }

    /// Deallocates the voice group. Does not stop the voices attached to the
    /// voice group.
    ///
    /// [handle] the group handle to destroy.
    FFI_PLUGIN_EXPORT void destroyVoiceGroup(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->destroyVoiceGroup(handle);
    }

    /// Adds voice handle to the voice group. The voice handles can still be
    /// used separate from the group.
    /// [voiceGroupHandle] the group handle to add the new [voiceHandle].
    /// [voiceHandle] voice handle to add to the [voiceGroupHandle].
    FFI_PLUGIN_EXPORT void addVoiceToGroup(unsigned int voiceGroupHandle, unsigned int voiceHandle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return;
        player.get()->addVoiceToGroup(voiceGroupHandle, voiceHandle);
    }

    /// Checks if the handle is a valid voice group. Does not care if the
    /// voice group is empty.
    ///
    /// [handle] the group handle to check.
    /// Return true if [handle] is a group handle.
    FFI_PLUGIN_EXPORT bool isVoiceGroup(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return false;
        return player.get()->isVoiceGroup(handle);
    }

    /// Checks whether a voice group is empty. SoLoud automatically trims
    /// the voice groups of voices that have ended, so the group may be
    /// empty even though you've added valid voice handles to it.
    ///
    /// [handle] group handle to check.
    /// Return true if the group handle doesn't have any voices.
    FFI_PLUGIN_EXPORT bool isVoiceGroupEmpty(unsigned int handle)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return false;
        return player.get()->isVoiceGroupEmpty(handle);
    }

    /////////////////////////////////////////
    /// faders & oscillators
    /////////////////////////////////////////

    /// Smoothly change the global volume over specified time.
    FFI_PLUGIN_EXPORT enum PlayerErrors fadeGlobalVolume(float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->fadeGlobalVolume(to, time);
        return noError;
    }

    /// Smoothly change a channel's volume over specified time.
    FFI_PLUGIN_EXPORT enum PlayerErrors fadeVolume(unsigned int handle, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->fadeVolume(handle, to, time);
        return noError;
    }

    /// Smoothly change a channel's pan setting over specified time.
    FFI_PLUGIN_EXPORT enum PlayerErrors fadePan(unsigned int handle, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->fadePan(handle, to, time);
        return noError;
    }

    /// Smoothly change a channel's relative play speed over specified time.
    FFI_PLUGIN_EXPORT enum PlayerErrors fadeRelativePlaySpeed(unsigned int handle, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->fadeRelativePlaySpeed(handle, to, time);
        return noError;
    }

    /// After specified time, pause the channel.
    FFI_PLUGIN_EXPORT enum PlayerErrors schedulePause(unsigned int handle, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->schedulePause(handle, time);
        return noError;
    }

    /// After specified time, stop the channel.
    FFI_PLUGIN_EXPORT enum PlayerErrors scheduleStop(unsigned int handle, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->scheduleStop(handle, time);
        return noError;
    }

    /// Set fader to oscillate the volume at specified frequency.
    FFI_PLUGIN_EXPORT enum PlayerErrors oscillateVolume(unsigned int handle, float from, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->oscillateVolume(handle, from, to, time);
        return noError;
    }

    /// Set fader to oscillate the panning at specified frequency.
    FFI_PLUGIN_EXPORT enum PlayerErrors oscillatePan(unsigned int handle, float from, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->oscillatePan(handle, from, to, time);
        return noError;
    }

    /// Set fader to oscillate the relative play speed at specified frequency.
    FFI_PLUGIN_EXPORT enum PlayerErrors oscillateRelativePlaySpeed(unsigned int handle, float from, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->oscillateRelativePlaySpeed(handle, from, to, time);
        return noError;
    }

    /// Set fader to oscillate the global volume at specified frequency.
    FFI_PLUGIN_EXPORT enum PlayerErrors oscillateGlobalVolume(float from, float to, float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        player.get()->oscillateGlobalVolume(from, to, time);
        return noError;
    }

    /////////////////////////////////////////
    /// Filters
    /////////////////////////////////////////

    /// Check if the given filter is active or not.
    ///
    /// [soundHash] the sound to check the filter. If this is =0 this function
    /// searches in the global filters.
    /// [filterType] filter to check.
    /// Returns [PlayerErrors.noError] if no errors and the index of
    /// the given filter (-1 if the filter is not active).
    FFI_PLUGIN_EXPORT enum PlayerErrors isFilterActive(unsigned int soundHash, enum FilterType filterType, int *index)
    {
        *index = -1;
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;

        if (soundHash == 0)
            *index = player.get()->mFilters.isFilterActive(filterType);
        else
        {
            auto const s = player.get()->findByHash(soundHash);
            if (s == nullptr)
                return soundHashNotFound;
            *index = s->filters->isFilterActive(filterType);
        }

        return noError;
    }

    /// Get parameters names of the given filter.
    ///
    /// [filterType] filter to get param names.
    /// Returns [PlayerErrors.noError] if no errors and the list of param names.
    FFI_PLUGIN_EXPORT enum PlayerErrors getFilterParamNames(
        enum FilterType filterType, int *paramsCount, char **names)
    {
        *paramsCount = 0;
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        std::vector<std::string> pNames = player.get()->mFilters.getFilterParamNames(filterType);
        *paramsCount = static_cast<int>(pNames.size());
        *names = (char *)malloc(sizeof(char *) * *paramsCount);
        // printf("C  paramsCount: %p  **names: %p\n", paramsCount, names);
        for (int i = 0; i < *paramsCount; i++)
        {
            names[i] = strdup(pNames[i].c_str());
            printf("C  i: %d  names[i]: %s  names[i]: %p\n", i, names[i], names[i]);
        }
        return noError;
    }

    /// Add the filter [filterType] to [soundHash]. If [soundHash]==0 the
    /// filter is added to global filters.
    ///
    /// [soundHash] the sound to add the filter to.
    /// [filterType] filter to add.
    /// Returns [PlayerErrors.noError] if no errors.
    FFI_PLUGIN_EXPORT enum PlayerErrors addFilter(unsigned int soundHash, enum FilterType filterType)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        if (soundHash == 0)
            return player.get()->mFilters.addFilter(filterType);

        auto const s = player.get()->findByHash(soundHash);
        if (s == nullptr)
            return soundHashNotFound;

        return s->filters->addFilter(filterType);
    }

    /// Remove the filter [filterType] from [soundHash]. If [soundHash]==0 the
    /// filter is removed from the global filters.
    ///
    /// [filterType] filter to remove.
    /// Returns [PlayerErrors.noError] if no errors.
    FFI_PLUGIN_EXPORT enum PlayerErrors removeFilter(unsigned int soundHash, enum FilterType filterType)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        if (soundHash == 0)
        {
            if (!player.get()->mFilters.removeFilter(filterType))
                return filterNotFound;
        }
        else
        {
            auto const s = player.get()->findByHash(soundHash);
            if (s == nullptr)
                return soundHashNotFound;
            if (!s->filters->removeFilter(filterType))
                return filterNotFound;
        }

        return noError;
    }

    /// Set the effect parameter with id [attributeId]
    /// of [filterType] with [value] value.
    ///
    /// [handle] the handle to set the filter to. If equal to 0, the filter is applyed globally.
    /// [filterType] filter to modify a param.
    /// Returns [PlayerErrors.noError] if no errors.
    FFI_PLUGIN_EXPORT enum PlayerErrors setFilterParams(unsigned int handle, enum FilterType filterType, int attributeId, float value)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        /// Not important to call the [SoLoud::AudioSource].setFilterParams() here, SoLoud
        /// will set the param globally or by [handle] if  [handle] is not ==0.
        if (handle == 0)
            player.get()->mFilters.setFilterParams(handle, filterType, attributeId, value);
        else
        {
            auto const &s = player.get()->findByHandle(handle);
            if (s == nullptr)
            {
                return soundHandleNotFound;
            }
            else
            {
                s->filters.get()->setFilterParams(handle, filterType, attributeId, value);
            }
        }
        return noError;
    }

    /// Get the effect parameter with id [attributeId] of [filterType].
    ///
    /// [handle] the handle to get the filter to. If equal to 0, it gets the global filter.
    /// [filterType] filter to modify a param.
    /// Returns the value of param or 9999.0 if the filter is not found.
    FFI_PLUGIN_EXPORT enum PlayerErrors getFilterParams(
        unsigned int handle,
        enum FilterType filterType,
        int attributeId,
        float *filterValue)
    {
        *filterValue = 9999.0f;
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        /// If [handle] == 0 get the parameter from global filters else from 
        /// the sound which owns [handle].
        if (handle == 0)
        {
            *filterValue = player.get()->mFilters.getFilterParams(handle, filterType, attributeId);
            return noError;
        }
        else
        {
            auto const &s = player.get()->findByHandle(handle);
            if (s == nullptr)
                return soundHandleNotFound;
            else
            {
                *filterValue = s->filters.get()->getFilterParams(handle, filterType, attributeId);
                if (*filterValue == 9999.0f)
                    return filterNotFound;
                return noError;
            }
        }
        return noError;
    }

    /// Fades a parameter of a filter.
    ///
    /// [handle] the handle of the voice to apply the fade. If equal to 0, it fades the global filter.
    /// [filterType] filter to modify a param.
    /// [attributeId] the attribute index to fade.
    /// [to] value the attribute should go in [time] duration.
    /// [time] the fade slope duration.
    /// Returns [PlayerErrors.noError] if no errors.
    FFI_PLUGIN_EXPORT enum PlayerErrors fadeFilterParameter(
        unsigned int handle,
        enum FilterType filterType,
        int attributeId,
        float to,
        float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        if (handle == 0)
            player.get()->mFilters.fadeFilterParameter(handle, filterType, attributeId, to, time);
        else
        {
            auto const &s = player.get()->findByHandle(handle);
            if (s == nullptr)
            {
                return soundHandleNotFound;
            }
            else
            {
                s->filters.get()->fadeFilterParameter(handle, filterType, attributeId, to, time);
            }
        }
        return noError;
    }

    /// Oscillate a parameter of a filter.
    ///
    /// [handle] the handle of the voice to apply the fade. If equal to 0, it fades the global filter.
    /// [filterType] filter to modify a param.
    /// [attributeId] the attribute index to fade.
    /// [from] the starting value the attribute sould start to oscillate.
    /// [to] the ending value the attribute sould end to oscillate.
    /// [time] the fade slope duration.
    /// Returns [PlayerErrors.noError] if no errors.
    FFI_PLUGIN_EXPORT enum PlayerErrors oscillateFilterParameter(
        unsigned int handle,
        enum FilterType filterType,
        int attributeId,
        float from,
        float to,
        float time)
    {
        if (player.get() == nullptr || !player.get()->isInited())
            return backendNotInited;
        if (handle == 0)
            player.get()->mFilters.oscillateFilterParameter(handle, filterType, attributeId, from, to, time);
        else
        {
            auto const &s = player.get()->findByHandle(handle);
            if (s == nullptr)
            {
                return soundHandleNotFound;
            }
            else
            {
                s->filters.get()->oscillateFilterParameter(handle, filterType, attributeId, from, to, time);
            }
        }
        return noError;
    }

    /////////////////////////////////////////
    /// 3D audio methods
    /////////////////////////////////////////

    /// play3d() is the 3d version of the play() call
    ///
    /// [posX], [posY], [posZ] are the audio source position coordinates.
    /// [velX], [velY], [velZ] are the audio source velocity.
    /// [looping] whether to start the sound in looping state.
    /// [loopingStartAt] If looping is enabled, the loop point is, by default,
    /// the start of the stream. The loop start point can be set with this parameter, and
    /// current loop point can be queried with [getLoopingPoint] and
    /// changed by [setLoopingPoint].
    /// Returns the handle of the sound, 0 if error
    FFI_PLUGIN_EXPORT PlayerErrors play3d(
        unsigned int soundHash,
        float posX,
        float posY,
        float posZ,
        float velX,
        float velY,
        float velZ,
        float volume,
        bool paused,
        bool looping,
        double loopingStartAt,
        unsigned int *handle)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return backendNotInited;

        *handle = player.get()->play3d(
            soundHash,
            posX, posY, posZ,
            velX, velY, velZ,
            volume,
            paused,
            0,
            looping,
            loopingStartAt);
        return *handle == 0 ? soundHashNotFound : noError;
    }

    /// You can set and get the current value of the speed of
    /// sound width the get3dSoundSpeed() and set3dSoundSpeed() functions.
    /// The speed of sound is used to calculate doppler effects in
    /// addition to the distance delay.

    /// Since SoLoud has no knowledge of the scale of your coordinates,
    /// you may need to adjust the speed of sound for these effects
    /// to work correctly. The default value is 343, which assumes
    /// that your world coordinates are in meters (where 1 unit is 1 meter),
    /// and that the environment is dry air at around 20 degrees Celsius.
    FFI_PLUGIN_EXPORT void set3dSoundSpeed(float speed)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSoundSpeed(speed);
        player.get()->update3dAudio();
    }

    /// Get the sound speed
    FFI_PLUGIN_EXPORT float get3dSoundSpeed()
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return 0.0f;
        return player.get()->get3dSoundSpeed();
    }

    /// You can set the position, at-vector, up-vector and velocity
    /// parameters of the 3d audio listener with one call
    FFI_PLUGIN_EXPORT void set3dListenerParameters(
        float posX, float posY, float posZ,
        float atX, float atY, float atZ,
        float upX, float upY, float upZ,
        float velocityX, float velocityY, float velocityZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dListenerParameters(
            posX, posY, posZ,
            atX, atY, atZ,
            upX, upY, upZ,
            velocityX, velocityY, velocityZ);
        player.get()->update3dAudio();
    }

    /// You can set the position parameter of the 3d audio listener
    FFI_PLUGIN_EXPORT void set3dListenerPosition(
        float posX,
        float posY,
        float posZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dListenerPosition(posX, posY, posZ);
        player.get()->update3dAudio();
    }

    /// You can set the "at" vector parameter of the 3d audio listener
    FFI_PLUGIN_EXPORT void set3dListenerAt(
        float atX,
        float atY,
        float atZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dListenerAt(atX, atY, atZ);
        player.get()->update3dAudio();
    }

    /// You can set the "up" vector parameter of the 3d audio listener
    FFI_PLUGIN_EXPORT void set3dListenerUp(
        float upX,
        float upY,
        float upZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dListenerAt(upX, upY, upZ);
        player.get()->update3dAudio();
    }

    /// You can set the listener's velocity vector parameter
    FFI_PLUGIN_EXPORT void set3dListenerVelocity(
        float velocityX,
        float velocityY,
        float velocityZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dListenerVelocity(velocityX, velocityY, velocityZ);
        player.get()->update3dAudio();
    }

    /// You can set the position and velocity parameters of a live
    /// 3d audio source with one call
    FFI_PLUGIN_EXPORT void set3dSourceParameters(
        unsigned int handle,
        float posX, float posY, float posZ,
        float velocityX, float velocityY, float velocityZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourceParameters(handle,
                                            posX, posY, posZ,
                                            velocityX, velocityY, velocityZ);
        player.get()->update3dAudio();
    }

    /// You can set the position parameters of a live 3d audio source
    FFI_PLUGIN_EXPORT void set3dSourcePosition(
        unsigned int handle,
        float posX,
        float posY,
        float posZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourcePosition(handle, posX, posY, posZ);
        player.get()->update3dAudio();
    }

    /// You can set the velocity parameters of a live 3d audio source
    FFI_PLUGIN_EXPORT void set3dSourceVelocity(
        unsigned int handle,
        float velocityX,
        float velocityY,
        float velocityZ)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourceVelocity(handle, velocityX, velocityY, velocityZ);
        player.get()->update3dAudio();
    }

    /// You can set the minimum and maximum distance parameters
    /// of a live 3d audio source
    FFI_PLUGIN_EXPORT void set3dSourceMinMaxDistance(
        unsigned int handle,
        float minDistance,
        float maxDistance)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourceMinMaxDistance(handle, minDistance, maxDistance);
        player.get()->update3dAudio();
    }

    /// You can change the attenuation model and rolloff factor parameters of
    /// a live 3d audio source.
    /// The default values are NO_ATTENUATION and 1.
    ///
    /// NO_ATTENUATION 	        No attenuation
    /// INVERSE_DISTANCE 	    Inverse distance attenuation model
    /// LINEAR_DISTANCE 	    Linear distance attenuation model
    /// EXPONENTIAL_DISTANCE 	Exponential distance attenuation model
    FFI_PLUGIN_EXPORT void set3dSourceAttenuation(
        unsigned int handle,
        unsigned int attenuationModel,
        float attenuationRolloffFactor)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourceAttenuation(handle, attenuationModel, attenuationRolloffFactor);
        player.get()->update3dAudio();
    }

    /// You can change the doppler factor of a live 3d audio source
    FFI_PLUGIN_EXPORT void set3dSourceDopplerFactor(
        unsigned int handle,
        float dopplerFactor)
    {
        if (player.get() == nullptr || !player.get()->isInited() ||
            player.get()->getSoundsCount() == 0)
            return;
        player.get()->set3dSourceDopplerFactor(handle, dopplerFactor);
        player.get()->update3dAudio();
    }

#ifdef __cplusplus
}
#endif

#pragma once

#ifndef PLAYER_H
#define PLAYER_H

#include "soloud.h"
#include "soloud_speech.h"
#include "enums.h"
#include "audiobuffer/buffer.h"
#include "filters/filters.h"
#include "active_sound.h"
#include "audiobuffer/audiobuffer.h"
#include "soloud/src/backend/miniaudio/miniaudio.h"

#include <iostream>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>

struct PlaybackDevice
{
    char *name;
    unsigned int isDefault;
    unsigned int id;
};

class Player
{
public:
    Player();
    ~Player();

    /// @brief Initialize the player. Must be called before any other player functions.
    /// @param sampleRate sample rate. Usually is 22050, 44100 (CD quality) or 48000.
    /// @param bufferSize the audio buffer size. Usually is 2048, but can be also 512 when
    /// low latency is needed for example in games.
    /// @param channels 1)mono, 2)stereo 4)quad 6)5.1 8)7.1
    /// @param deviceID the device ID. -1 for default OS output device.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors init(unsigned int sampleRate, unsigned int bufferSize, unsigned int channels, int deviceID = -1);

    /// @brief Change the playback device.
    /// @param deviceID the device ID. -1 for default OS output device.
    PlayerErrors changeDevice(int deviceID);

    std::vector<PlaybackDevice> listPlaybackDevices();

    /// @brief Set a function callback triggered when a voice is stopped/ended.
    void setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int *));

    /// @brief Set a function callback triggered when the state of the player changes.
    void setStateChangedCallback(void (*stateChangedCallback)(unsigned int));

    /// @brief Must be called when there is no more need of the player or when closing the app.
    void dispose();

    /// @brief Return true if the player has been initialized.
    bool isInited();

    /// @brief Return the active sounds.
    int getSoundsCount();

    /// @brief Returns human readable string of an error.
    /// @param errorCode the error code.
    /// @return a string represented by the PlayerErrors code.
    const std::string getErrorString(PlayerErrors errorCode) const;

    /// @brief Load a new sound to be played once or multiple times later.
    /// @param completeFileName the complete file path + file name.
    /// @param loadIntoMem if true Soloud::wav will be used which loads
    /// all raw audio data into memory. This will be useful when
    /// the audio is short, ie for game sounds, mainly used to prevent
    /// gaps or lags when starting a sound (less CPU, more memory allocated).
    /// (https://solhsa.com/soloud/wav.html)
    /// If false, the audio data is loaded from the given file when
    /// needed (more CPU less memory allocated). (https://solhsa.com/soloud/wavstream.html)
    /// @param hash return the hash of the sound.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    ///
    /// NOTE: non standard OGGs file with a custom header introduced by `xiph` cannot be playd
    /// and the `stb_vorbis::start_decoder` returns VORBIS_invalid_first_page error.
    /// ref: https://github.com/nothings/stb/issues/676
    PlayerErrors loadFile(
        const std::string &completeFileName,
        const bool loadIntoMem,
        unsigned int *hash);

    /// @brief Load a new sound stored into [mem] to be played once or multiple times later.
    /// Mainly used on web because the browsers are not allowed to read files directly.
    /// @param uniqueName the unique name of the sound. Used only to have the [hash].
    /// @param mem the audio data. These contains the audio file bytes.
    /// @param length the length of [mem].
    /// @param hash return the hash of the sound.
    PlayerErrors loadMem(
        const std::string &uniqueName,
        unsigned char *mem,
        int length,
        bool loadIntoMem,
        unsigned int &hash);

    /// @brief Set up an audio stream.
    /// @param hash return the hash of the sound.
    /// @param maxBufferSize the max buffer size in bytes.
    /// @param bufferingType the buffering type.
    /// @param isPCM if true, the audio data is PCM.
    /// @param dataType in case the audio data is PCM, here are the parameters to set it up.
    PlayerErrors setBufferStream(
        unsigned int &hash,
        unsigned long maxBufferSize,
        BufferingType bufferingType,
        SoLoud::time bufferingTimeNeeds,
        PCMformat pcmFormat = {44100, 2, 4, PCM_F32LE},
        dartOnBufferingCallback_t onBufferingCallback = nullptr);

    /// @brief Resets the buffer of the data stream.
    /// @param hash the hash of the sound.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors resetBufferStream(unsigned int hash);

    /// @brief Get the time consumed by the data stream of type `BufferingType.RELEASED`.
    /// @param hash the hash of the stream sound.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors getStreamTimeConsumed(unsigned int hash, float *timeConsumed);

    /// @brief Add an audio data stream.
    /// @param hash the hash of the sound.
    /// @param data the audio data to add.
    /// @param aDataLen the length of [data].
    PlayerErrors addAudioDataStream(
        unsigned int hash,
        const unsigned char *data,
        unsigned int aDataLen);

    /// @brief Set the end of the data stream.
    /// @param hash the hash of the sound.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors setDataIsEnded(unsigned int hash);

    /// @brief Get the current buffer size in bytes of this sound with hash [hash].
    /// @param hash the hash of the stream sound.
    /// @param sizeInBytes return the size in bytes.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors getBufferSize(unsigned int hash, unsigned int *sizeInBytes);

    /// @brief Load a new sound which will be generated by the given params.
    /// @param waveform the type of [SoLoud::Soloud::WAVEFORM] to generate.
    /// @param superWave whater this is a superWave.
    /// @param scale if using [superWave] this is its scale.
    /// @param detune if using [superWave] this is its detune.
    /// @param hash the hash code of the new generated sound.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    PlayerErrors loadWaveform(
        int waveform,
        bool superWave,
        float scale,
        float detune,
        unsigned int &hash);

    /// @brief If this sound is you can change the scale at runtime.
    /// @param soundHash the sound hash to change the scale to.
    /// @param newScale the new scale.
    void setWaveformScale(unsigned int soundHash, float newScale);

    /// @brief If this sound is you can change the detune at runtime.
    /// @param soundHash the sound hash to change the scale to.
    /// @param newDetune the new detune.
    void setWaveformDetune(unsigned int soundHash, float newDetune);

    /// @brief Set the frequency of the given sound.
    /// @param soundHash the hash of the sound.
    /// @param newFreq the new frequency.
    void setWaveformFreq(unsigned int soundHash, float newFreq);

    /// @brief Set the given sound as a super wave.
    /// @param soundHash the sound hash to change the scale to.
    /// @param superwave whether this sound should be a super wave or not.
    void setWaveformSuperwave(unsigned int soundHash, bool superwave);

    /// @brief Set a waveform type to the given sound: see [SoLoud::Soloud::WAVEFORM] enum.
    /// @param soundHash the sound hash to change the wafeform type.
    /// @param newWaveform the new waveform type.
    void setWaveform(unsigned int soundHash, int newWaveform);

    /// @brief Switch pause state for an already loaded sound identified by [handle].
    /// @param handle the sound handle
    void pauseSwitch(unsigned int handle);

    /// @brief Pause or unpause already loaded sound identified by [handle].
    /// @param handle the sound handle.
    /// @param pause whether this sound should be paused or not.
    void setPause(unsigned int handle, bool pause);

    /// @brief Gets the pause state.
    /// @param handle the sound handle.
    /// @return true if paused.
    bool getPause(unsigned int handle);

    /// @brief Set a sound's relative play speed.
    /// Setting the value to 0 will cause undefined behavior, likely a crash.
    /// Change the relative play speed of a sample. This changes the effective
    /// sample rate while leaving the base sample rate alone.
    ///
    /// Note that playing a sound at a higher sample rate will require SoLoud
    /// to request more samples from the sound source, which will require more
    /// memory and more processing power. Playing at a slower sample rate is cheaper.
    /// @param handle the sound handle.
    /// @param speed the new speed.
    void setRelativePlaySpeed(unsigned int handle, float speed);

    /// @brief Get a sound's relative play speed.
    /// If an invalid handle is given to getRelativePlaySpeed, it will return 1.
    /// @param handle the sound handle.
    /// @return the current play speed.
    float getRelativePlaySpeed(unsigned int handle);

    /// @brief Play already loaded sound identified by [soundHash].
    /// @param soundHash the unique hash of the sound to play.
    /// @param volume 1.0f full volume.
    /// @param pan 0.0f centered.
    /// @param paused 0 not pause.
    /// @param looping whether to start the sound in looping state.
    /// @param loopingStartAt If looping is enabled, the loop point is, by default,
    /// the start of the stream. The loop start point can be set with this parameter, and
    /// current loop point can be queried with [getLoopingPoint] and
    /// changed by [setLoopingPoint].
    /// @return the handle of the sound, 0 if error.
    PlayerErrors play(
        unsigned int soundHash,
        unsigned int &handle,
        float volume = 1.0f,
        float pan = 0.0f,
        bool paused = false,
        bool looping = false,
        double loopingStartAt = 0.0);

    /// @brief Stop already loaded sound identified by [handle] and clear it.
    /// @param handle handle of the sound.
    void stop(unsigned int handle);

    /// @brief Remove the unique [handle] form the list of internal sounds.
    /// @param handle handle of the sound.
    void removeHandle(unsigned int handle);

    /// @brief Stop all handles of the already loaded sound identified by [soundHash] and clear it.
    /// @param soundHash hash of the sound.
    void disposeSound(unsigned int soundHash);

    /// @brief Dispose all sounds already loaded.
    void disposeAllSound();

    /// @brief Ask whether a sound is set to loop or not.
    bool getLooping(unsigned int handle);

    /// @brief This function can be used to set a sample to play on repeat,
    /// instead of just playing once.
    /// @param handle handle of the sound.
    /// @param enable whether to enable looping or not.
    void setLooping(unsigned int handle, bool enable);

    /// @brief Get sound loop point value.
    /// @param handle handle of the sound.
    /// @return the time in seconds.
    double getLoopPoint(unsigned int handle);

    /// @brief Set sound loop point value.
    /// @param handle handle of the sound.
    /// @param time in seconds.
    void setLoopPoint(unsigned int handle, double time);

    /// @brief Speech the given text.
    /// @param textToSpeech the text to be spoken.
    /// @param handle handle of the sound. Set to -1 if error.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    /// TODO(marco): add other T2S parameters
    PlayerErrors textToSpeech(const std::string &textToSpeech, unsigned int &handle);

    /// @brief Enable or disable visualization
    /// @param enabled setting this to true will enable to get wave and FFT data.
    void setVisualizationEnabled(bool enabled);

    /// @brief Returns true if the visualization is enable. If so the wave and FFT
    /// data che be get with `calcFFT()` and `getWave()`.
    bool isVisualizationEnabled();

    /// @brief Calculates FFT of the currently playing sound.
    /// @return a 256 float pointer to the result.
    float *calcFFT(bool *isTheSameAsBefore);

    /// @brief Gets 256 samples of the currently playing sound.
    /// @return a 256 float pointer to the result.
    float *getWave(bool *isTheSameAsBefore);

    /// @brief get the sound length in seconds.
    /// @param soundHash the sound hash.
    /// @return returns sound length in seconds.
    double getLength(unsigned int soundHash);

    /// @brief Seek playing in seconds.
    /// @param handle the sound handle.
    /// @param time the time to seek in seconds.
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success.
    ///
    /// WARNING: when seeking an mp3 file loaded using `loadIntoMem`=false
    /// the seek operation is not performed due to the problem explained
    /// in souloud_wavstream.cpp in `WavStreamInstance::seek` function.
    PlayerErrors seek(SoLoud::handle handle, float time);

    /// @brief Get current sound position in seconds.
    /// @return time in seconds.
    double getPosition(SoLoud::handle handle);

    /// @brief Get current global volume.
    /// @return the volume.
    float getGlobalVolume();

    /// @brief Set the global volume for all the sounds.
    /// @param volume the new volume to set.
    void setGlobalVolume(float volume);

    /// @brief Get current [handle] volume.
    /// @return the volume.
    float getVolume(SoLoud::handle handle);

    /// @brief Set the [handle] volume.
    /// @param handle the sound handle.
    /// @param volume the new volume to set.
    void setVolume(SoLoud::handle handle, float volume);

    /// @brief Get a sound's current pan setting.
    /// @param handle the sound handle.
    /// @return the range of the pan values is -1 to 1, where -1 is left, 0 is middle and and 1 is right.
    float getPan(SoLoud::handle handle);

    /// @brief Set a sound's current pan setting.
    /// @param handle the sound handle.
    /// @param pan the range of the pan values is -1 to 1, where -1 is left, 0 is middle and and 1 is right.
    void setPan(SoLoud::handle handle, float pan);

    /// @brief Set the left/right volumes directly.
    /// Note that this does not affect the value returned by getPan.
    /// @param handle the sound handle.
    /// @param panLeft value for the left pan.
    /// @param panRight value for the right pan.
    void setPanAbsolute(SoLoud::handle handle, float panLeft, float panRight);

    /// @brief Check if a handle is still valid.
    /// @param handle handle to check.
    /// @return true if it still exists.
    bool isValidHandle(SoLoud::handle handle);

    /// @brief Return the number of active voices summing up all the handles of each sound.
    /// The difference between this function and getActiveVoiceCount() is that
    /// getActiveVoiceCount() returns the number of active removing those that are paused or
    /// with a very low volume, while this function returns the total number of active voices.
    unsigned int getActiveVoiceCount_internal();

    /// @brief Returns the number of concurrent sounds that are playing a specific audio source.
    int countAudioSource(unsigned int soundHash);

    /// @brief Returns the number of voices the application has told SoLoud to play.
    unsigned int getVoiceCount();

    /// @brief Get a sound's protection state.
    bool getProtectVoice(SoLoud::handle handle);

    /// @brief Set a sound's protection state.
    /// Normally, if you try to play more sounds than there are voices,
    /// SoLoud will kill off the oldest playing sound to make room.
    /// This will most likely be your background music. This can be worked
    /// around by protecting the sound.
    /// If all voices are protected, the result will be undefined.
    /// @param handle  handle to check.
    /// @param protect whether to protect or not.
    ///
    /// NOTE: patched with
    /// https://github.com/jarikomppa/soloud/issues/298
    void setProtectVoice(SoLoud::handle handle, bool protect);

    /// @brief Set the inaudible behavior of a live sound. By default,
    /// if a sound is inaudible, it's paused, and will resume when it
    /// becomes audible again. With this function you can tell SoLoud
    /// to either kill the sound if it becomes inaudible, or to keep
    /// ticking the sound even if it's inaudible.
    void setInaudibleBehavior(SoLoud::handle handle, bool mustTick, bool kill);

    /// @brief Get the current maximum active voice count.
    unsigned int getMaxActiveVoiceCount();

    /// @brief set the current maximum active voice count.
    /// If voice count is higher than the maximum active voice count,
    /// SoLoud will pick the ones with the highest volume to actually play.
    /// @param maxVoiceCount the max concurrent sounds that can be played.
    ///
    /// NOTE: The number of concurrent voices is limited, as having unlimited
    /// voices would cause performance issues, as well as lead to unnecessary clipping.
    /// The default number of concurrent voices is 16, but this can be adjusted at runtime.
    /// The hard maximum number is 4095, but if more are required, SoLoud can be modified to
    /// support more. But seriously, if you need more than 4095 sounds at once, you're
    /// probably going to make some serious changes in any case.
    void setMaxActiveVoiceCount(unsigned int maxVoiceCount);

    /// @brief Find a sound by its handle.
    /// @param handle the handle to search.
    /// @return If not found, return nullptr.
    ActiveSound *findByHandle(SoLoud::handle handle);

    /// @brief Find a sound by its handle.
    /// @param hash the hash to search.
    /// @return If not found, return nullptr.
    ActiveSound *findByHash(unsigned int hash);

    void debug();

    /////////////////////////////////////////
    /// voice groups
    /////////////////////////////////////////

    /// @brief Used to create a new voice group. Returns 0 if not successful.
    unsigned int createVoiceGroup();

    /// @brief Deallocates the voice group. Does not stop the voices attached to the voice group.
    /// @param handle the group handle to destroy.
    void destroyVoiceGroup(SoLoud::handle handle);

    /// @brief Adds voice handle to the voice group. The voice handles can still be used separate from the group.
    /// @param voiceGroupHandle the group handle to add the new [handle].
    /// @param voiceHandle voice handle to add to the [voiceGroupHandle].
    void addVoiceToGroup(SoLoud::handle voiceGroupHandle, SoLoud::handle voiceHandle);

    /// @brief Checks if the handle is a valid voice group. Does not care if the voice group is empty.
    /// @param handle the group handle to check.
    /// @return true if [handle] is a group handle.
    bool isVoiceGroup(SoLoud::handle handle);

    /// @brief Checks whether a voice group is empty. SoLoud automatically trims the voice groups of
    /// voices that have ended, so the group may be empty even though you've added valid voice handles to it.
    /// @param handle group handle to check.
    /// @return true if the group handle doesn't have any voices.
    bool isVoiceGroupEmpty(SoLoud::handle handle);

    /////////////////////////////////////////
    /// faders & oscillators
    /////////////////////////////////////////

    /// @brief Smoothly change the global volume over specified time.
    /// @param to the volume to fade to.
    /// @param time the time in seconds to change the volume.
    void fadeGlobalVolume(float to, float time);

    /// @brief Smoothly change the sound volume over specified time.
    /// @param handle the sound handle.
    /// @param to the volume to fade to.
    /// @param time the time in seconds to change the volume.
    void fadeVolume(SoLoud::handle handle, float to, float time);

    /// @brief Smoothly change a channel's pan setting over specified time.
    /// @param handle the sound handle.
    /// @param to the pan value to fade to.
    /// @param time the time in seconds to change the pan.
    void fadePan(SoLoud::handle handle, float to, float time);

    /// @brief Smoothly change a channel's relative play speed over specified time.
    /// @param handle the sound handle.
    /// @param to the speed value to fade to.
    /// @param time the time in seconds to change the speed.
    void fadeRelativePlaySpeed(SoLoud::handle handle, float to, float time);

    /// @brief After specified time, pause the channel.
    /// @param handle the sound handle.
    /// @param time the time in seconds to pause.
    void schedulePause(SoLoud::handle handle, float time);

    /// @brief After specified time, pause the channel.
    /// @param handle the sound handle.
    /// @param time the time in seconds to pause.
    // TODO(marco): see if it is possible to use scheduleStop() to use it as loop-end position
    void scheduleStop(SoLoud::handle handle, float time);

    /// @brief Set fader to oscillate the volume at specified frequency.
    /// @param handle the sound handle.
    /// @param from the lowest value for the oscillation.
    /// @param to the highest value for the oscillation.
    /// @param time the time in seconds to oscillate.
    void oscillateVolume(SoLoud::handle handle, float from, float to, float time);

    /// @brief Set fader to oscillate the panning at specified frequency.
    /// @param handle the sound handle.
    /// @param from the lowest value for the oscillation.
    /// @param to the highest value for the oscillation.
    /// @param time the time in seconds to oscillate.
    void oscillatePan(SoLoud::handle handle, float from, float to, float time);

    /// @brief Set fader to oscillate the relative play speed at specified frequency.
    /// @param handle the sound handle.
    /// @param from the lowest value for the oscillation.
    /// @param to the highest value for the oscillation.
    /// @param time the time in seconds to oscillate.
    void oscillateRelativePlaySpeed(SoLoud::handle handle, float from, float to, float time);

    /// @brief Set fader to oscillate the global volume at specified frequency.
    /// @param from the lowest value for the oscillation.
    /// @param to the highest value for the oscillation.
    /// @param time the time in seconds to oscillate.
    void oscillateGlobalVolume(float from, float to, float time);

    /////////////////////////////////////////
    /// 3D audio
    /////////////////////////////////////////

    /// @brief apply change to voices when changing some 3d audio params.
    void update3dAudio();

    /// @brief play3d() is the 3d version of the play() call.
    /// @param looping whether to start the sound in looping state.
    /// @param loopingStartAt If looping is enabled, the loop point is, by default,
    /// the start of the stream. The loop start point can be set with this parameter, and
    /// current loop point can be queried with [getLoopingPoint] and
    /// changed by [setLoopingPoint].
    /// @return the handle of the sound, 0 if error.
    PlayerErrors play3d(
        unsigned int soundHash,
        unsigned int &handle,
        float posX,
        float posY,
        float posZ,
        float velX = 0.0f,
        float velY = 0.0f,
        float velZ = 0.0f,
        float volume = 1.0f,
        bool paused = 0,
        unsigned int bus = 0,
        bool looping = false,
        double loopingStartAt = 0.0);

    /// You can set and get the current value of the speed of
    /// sound width the get3dSoundSpeed() and set3dSoundSpeed() functions.
    /// The speed of sound is used to calculate doppler effects in
    /// addition to the distance delay.

    /// Since SoLoud has no knowledge of the scale of your coordinates,
    /// you may need to adjust the speed of sound for these effects
    /// to work correctly. The default value is 343, which assumes
    /// that your world coordinates are in meters (where 1 unit is 1 meter),
    /// and that the environment is dry air at around 20 degrees Celsius.
    void set3dSoundSpeed(float speed);
    float get3dSoundSpeed();

    void set3dListenerParameters(
        float posX, float posY, float posZ,
        float atX, float atY, float atZ,
        float upX, float upY, float upZ,
        float velocityX, float velocityY, float velocityZ);
    void set3dListenerPosition(float posX,
                               float posY,
                               float posZ);
    void set3dListenerAt(float atX,
                         float atY,
                         float atZ);
    void set3dListenerUp(float upX,
                         float upY,
                         float upZ);
    void set3dListenerVelocity(float velocityX,
                               float velocityY,
                               float velocityZ);

    void set3dSourceParameters(unsigned int handle,
                               float posX,
                               float posY,
                               float posZ,
                               float velocityX,
                               float velocityY,
                               float velocityZ);
    void set3dSourcePosition(unsigned int handle,
                             float posX,
                             float posY,
                             float posZ);
    void set3dSourceVelocity(unsigned int handle,
                             float velocityX,
                             float velocityY,
                             float velocityZ);
    void set3dSourceMinMaxDistance(unsigned int handle,
                                   float minDistance,
                                   float maxDistance);
    void set3dSourceAttenuation(unsigned int handle,
                                unsigned int attenuationModel,
                                float attenuationRolloffFactor);
    void set3dSourceDopplerFactor(unsigned int handle,
                                  float dopplerFactor);

public:
    /// all the sounds loaded
    std::vector<std::unique_ptr<ActiveSound>> sounds;

    /// true when the backend is initialized
    bool mInited;

    /// main SoLoud engine
    SoLoud::Soloud soloud;

    /// speech object
    SoLoud::Speech speech;

    /// Global filters
    Filters mFilters;

    unsigned int mSampleRate;

    unsigned int mChannels;

private:
    ma_device_info *pPlaybackInfos;
    std::mutex remove_handle_mutex;
    unsigned int mBufferSize;
};

#endif // PLAYER_H

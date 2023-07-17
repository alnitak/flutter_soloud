#pragma once

#ifndef PLAYER_H
#define PLAYER_H

#include "enums.h"
#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_speech.h"

#include <iostream>
#include <vector>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>
#include <thread>

typedef enum PlayerMessages
{
    MSG_NONE,
    MSG_STOP
} PlayerMessages_t;

/// The default number of concurrent voices - maximum number of "streams" - is 16, 
/// but this can be adjusted at runtime
struct ActiveSound {
    SoLoud::Wav sound;
    std::string completeFileName;
    /// many istances of [sound] can be played without re-loading it
    std::vector<SoLoud::handle> handle;
    /// following params are not used since 
    /// I can't call Dart from another thread
    double currPos = 0.0;
    double posForCallback = -1.0;
    void (*playEndedCallback)(unsigned int) = nullptr;
    void (*positionCallback)(void) = nullptr;
};

class Player {
public:
    Player();
    ~Player();

    /// @brief Initialize the player. Must be called before any other player functions
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    PlayerErrors init();

    /// @brief Must be called when there is no more need of the player or when closing the app
    /// @return 
    void dispose();

    bool isInited();
    int getSoundsCount();

    /// @brief 
    /// @param aErrorCode 
    /// @return a string represented by the PlayerErrors code
    const std::string getErrorString(PlayerErrors aErrorCode) const;

    /// @brief Play a new file
    /// @param completeFileName the complete file path + file name
    /// @param handle return handle of the sound. -1 if error
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    PlayerErrors play(const std::string& completeFileName, unsigned int &handle);

    /// @brief Pause or unpause already loaded sound identified by [handle]
    /// @param handle the sound handle
    void pauseSwitch(unsigned int handle);

    /// @brief Gets the pause state
    /// @param handle the sound handle
    /// @return true if paused
    bool getPause(unsigned int handle);
    
    /// @brief Play already loaded sound identified by [handle]
    /// @param handle 
    /// @return the handle of the sound
    unsigned int play(unsigned int handle);
    
    /// @brief Stop already loaded sound identified by [handle] and clear it
    /// @param handle 
    void stop(unsigned int handle);

    /// @brief Speech
    /// @param textToSpeech
    /// @param handle handle of the sound. -1 if error
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    /// TODO: add other T2S parameters
    PlayerErrors textToSpeech(const std::string& textToSpeech, unsigned int &handle);

    /// @brief Enable or disable visualization
    /// @param enabled 
    /// @return 
    void setVisualizationEnabled(bool enabled);

    /// @brief Calculates FFT of the currently playing sound
    /// @param
    /// @return a 256 float pointer to the result
    float* calcFFT();

    /// @brief Gets 256 samples of the currently playing sound
    /// @param
    /// @return a 256 float pointer to the result
    float* getWave();

    /// @brief get the sound length in seconds
    /// @return returns sound length in seconds
    double getLength(SoLoud::handle handle);

    /// @brief seek playing in [time] seconds
    /// @param handle the sound handle
    /// @param time the time to seek in seconds
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    PlayerErrors seek(SoLoud::handle handle, float time);

    /// @brief get current sound position
    /// @return time in seconds
    double getPosition(SoLoud::handle handle);

    /// @brief check if a handle is still valid.
    /// @param handle handle to check
    /// @return true if it still exists
    bool getIsValidVoiceHandle(SoLoud::handle handle);

    /// @brief Find a sound by its handle
    /// @param handle 
    /// @return If not found, return nullptr. 
    ///    [handleId] is the index of the handles of the sound found
    ActiveSound* findByHandle(SoLoud::handle handle, int *handleId); 

    void debug();

public:

    std::thread loopThread;
    std::atomic<bool> isLoopRunning;
    std::mutex mutex;
    std::vector<PlayerMessages> msg;
    std::vector<std::unique_ptr<ActiveSound>> sounds;

    /// true when the backend is initialized
    bool mInited;

    /// main SoLoud engine
    SoLoud::Soloud soloud;

    /// speech object
    SoLoud::Speech speech;

};


#endif // PLAYER_H

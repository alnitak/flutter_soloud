#pragma once

#ifndef PLAYER_H
#define PLAYER_H

#include "enums.h"
#include "soloud.h"
#include "soloud_wav.h"
#include "soloud_speech.h"

#include <iostream>

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

    /// @brief 
    /// @param aErrorCode 
    /// @return a string represented by the PlayerErrors code
    const std::string getErrorString(PlayerErrors aErrorCode) const;

    /// @brief Play a new file
    /// @param completeFileName the complete file path
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    PlayerErrors play(const std::string& completeFileName);

    /// @brief Speech
    /// @param textToSpeech
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    /// TODO: add other T2S parameters
    PlayerErrors textToSpeech(const std::string& textToSpeech);

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
    double getLength();

    /// @brief seek playing in [time] seconds
    /// @param time 
    /// @return Returns [PlayerErrors.SO_NO_ERROR] if success
    PlayerErrors seek(float time);

    /// @brief get current sound position
    /// @return time in seconds
    double getPosition();

public:
    /// true when the backend is initialized
    bool mInited;

    /// main SoLoud engine
    SoLoud::Soloud soloud;

    /// speech object
    SoLoud::Speech speech;

    /// audio object
    SoLoud::Wav sound;

    /// handle to the current playing sound
    SoLoud::handle currPlayingHandle{};

};


#endif // PLAYER_H

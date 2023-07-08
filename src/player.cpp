#include "player.h"
#include "soloud/include/soloud.h"
#include "soloud/src/backend/miniaudio/miniaudio.h"

Player::Player() : mInited(false) {};
Player::~Player() = default;

PlayerErrors Player::init() {
    // initialize SoLoud.
     SoLoud::result result = soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF, 
        SoLoud::Soloud::MINIAUDIO, 44100, 2048, 2U);
    // soloud.init(1U, 0U, 44100, 2048, 2U);
    // SoLoud::Thread::sleep(100);
    if (result == SoLoud::SO_NO_ERROR) 
        mInited = true;
    else 
        result = backendNotInited;
    return (PlayerErrors)result;
}

void Player::dispose() {
    // Clean up SoLoud
    soloud.deinit();
    mInited = false;
}

const std::string Player::getErrorString(PlayerErrors aErrorCode) const
{
    switch (aErrorCode)
    {
        case noError: return "No error";
        case invalidParameter: return "Some parameter is invalid";
        case fileNotFound: return "File not found";
        case fileLoadFailed: return "File found, but could not be loaded";
        case dllNotFound: return "DLL not found, or wrong DLL";
        case outOfMemory: return "Out of memory";
        case notImplemented: return "Feature not implemented";
        case backendNotInited: return "Player not yet initialized";
        /*case unknownError: return "Other error";*/
    }
    return "Other error";
}

PlayerErrors Player::play(const std::string& completeFileName) {
    if (!mInited) return backendNotInited;

    SoLoud::result result = sound.load(completeFileName.c_str());
    if (result == SoLoud::SO_NO_ERROR)
        currPlayingHandle = soloud.play(sound, -1.0f, 0.0f, 0, 0);
    return (PlayerErrors)result;
}

PlayerErrors Player::textToSpeech(const std::string& textToSpeech) {
    if (!mInited) return backendNotInited;
	
    SoLoud::result result = speech.setText(textToSpeech.c_str());
    if (result == SoLoud::SO_NO_ERROR)
	    soloud.play(speech);
    return (PlayerErrors)result;
}

void Player::setVisualizationEnabled(bool enabled) {
	soloud.setVisualizationEnable(enabled);
}

float* Player::calcFFT() {
	return soloud.calcFFT();
}

float* Player::getWave() {
	return soloud.getWave();
}

// The length in seconds
double Player::getLength() {
	double t = sound.getLength();
	return t;
}

// time in seconds
PlayerErrors Player::seek(float time) {
    if (!mInited) return backendNotInited;
	
    SoLoud::result result = soloud.seek(currPlayingHandle, time);
    return (PlayerErrors)result;
}

// time in seconds
double Player::getPosition() {
	return soloud.getStreamPosition(currPlayingHandle);
}
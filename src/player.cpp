#include "player.h"
#include "soloud.h"

#include <thread>
#include <algorithm>
#include <cstdarg>

#define RANGE_POSITION_CALLBACK 0.15

Player::Player() : mInited(false){};
Player::~Player() = default;

void Player::startLoop()
{
    PlayerMessages _msg;
    std::thread loopThread = std::thread([&](Player *me)
                                         {
        bool isLoopRunning = true;
        while(isLoopRunning) {
            mutex.lock();
            if (msg.size() == 0) _msg = MSG_NONE;
            else { _msg = msg.back(); msg.pop_back(); }

            switch (_msg)
            {
                case MSG_STOP: isLoopRunning = false; break;
                case MSG_NONE:
                default: {

                }

            }

            mutex.unlock();
        } },
                                         this);
    loopThread.detach();
}

void Player::stopLoop()
{
    msg.push_back(MSG_STOP);
}

////////////////////////////////////////////////////
////////////////////////////////////////////////////
////////////////////////////////////////////////////
PlayerErrors Player::init()
{
    if (mInited) dispose();
    
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

void Player::dispose()
{
    // Clean up SoLoud
    soloud.deinit();
    mInited = false;
    sounds.clear();
}

bool Player::isInited()
{
    return mInited;
}

int Player::getSoundsCount()
{
    return sounds.size();
}

const std::string Player::getErrorString(PlayerErrors aErrorCode) const
{
    switch (aErrorCode)
    {
    case noError:
        return "No error";
    case invalidParameter:
        return "Some parameter is invalid";
    case fileNotFound:
        return "File not found";
    case fileLoadFailed:
        return "File found, but could not be loaded";
    case dllNotFound:
        return "DLL not found, or wrong DLL";
    case outOfMemory:
        return "Out of memory";
    case notImplemented:
        return "Feature not implemented";
    case backendNotInited:
        return "Player not yet initialized";
    case unknownError:
        return "Unknown error";
    }
    return "Other error";
}

PlayerErrors Player::play(const std::string &completeFileName, unsigned int &handle)
{
    if (!mInited)
        return backendNotInited;

    sounds.push_back(std::make_unique<ActiveSound>());
    sounds.back().get()->completeFileName = std::string(completeFileName);
    SoLoud::result result = sounds.back().get()->sound.load(completeFileName.c_str());
    if (result == SoLoud::SO_NO_ERROR)
    {
        handle = sounds.back().get()->handle = soloud.play(sounds.back().get()->sound, -1.0f, 0.0f, 0, 0);
    }
    else
    {
        sounds.emplace_back();
    }
    return (PlayerErrors)result;
}

void Player::pause(unsigned int handle)
{
    ActiveSound* sound = findByHandle(handle);
    if (sound == nullptr) return;
    sound->sound.stop();
}

void Player::play(unsigned int handle)
{
    ActiveSound* sound = findByHandle(handle);
    if (sound == nullptr) return;
    soloud.play(sound->sound);
}

void Player::stop(unsigned int handle)
{
    ActiveSound* sound = findByHandle(handle);
    if (sound == nullptr) return;
    sound->sound.stop();
    sounds.erase( std::remove_if(sounds.begin(), sounds.end(),
                        [handle](std::unique_ptr<ActiveSound> &f)
                        {return f.get()->handle == handle;}) );
}

PlayerErrors Player::textToSpeech(const std::string &textToSpeech, unsigned int &handle)
{
    if (!mInited)
        return backendNotInited;

    sounds.push_back(std::make_unique<ActiveSound>());
    sounds.back().get()->completeFileName = std::string("");
    SoLoud::result result = speech.setText(textToSpeech.c_str());
    if (result == SoLoud::SO_NO_ERROR)
    {
        handle = sounds.back().get()->handle = soloud.play(speech);
    }
    else
    {
        sounds.emplace_back();
    }
    return (PlayerErrors)result;
}

void Player::setVisualizationEnabled(bool enabled)
{
    soloud.setVisualizationEnable(enabled);
}

float *Player::calcFFT()
{
    return soloud.calcFFT();
}

float *Player::getWave()
{
    return soloud.getWave();
}

// The length in seconds
double Player::getLength(SoLoud::handle handle)
{
    ActiveSound* sound = findByHandle(handle);
    if (sound == nullptr) return 0.0;
    return sound->sound.getLength();
}

// time in seconds
PlayerErrors Player::seek(SoLoud::handle handle, float time)
{
    if (!mInited)
        return backendNotInited;

    SoLoud::result result = soloud.seek(handle, time);
    return (PlayerErrors)result;
}

// returns time in seconds
double Player::getPosition(SoLoud::handle handle)
{
    ActiveSound* sound = findByHandle(handle);
    if (sound == nullptr) return 0.0;
    return soloud.getStreamPosition(sound->handle);
}

ActiveSound* Player::findByHandle(SoLoud::handle handle) 
{
    auto sound = std::find_if(sounds.begin(), sounds.end(),
                              [handle](std::unique_ptr<ActiveSound> &f)
                              { return f.get()->handle == handle; });
    if (sound == sounds.end()) return nullptr;
    return sound->get();
}

void Player::debug()
{
    int n=0;
    for (auto& sound : sounds) {
        printf("%d: \thandle: %d  \tpos: %f   %s\n", 
            n, sound.get()->handle, sound.get()->currPos, sound.get()->completeFileName.c_str());
        n++;
    }
}

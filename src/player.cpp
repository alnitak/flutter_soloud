#include "common.h"
#include "player.h"
#include "soloud.h"

#include <algorithm>
#include <cstdarg>
#ifdef _IS_WIN_
#include <stddef.h> // for size_t
#else
#include <unistd.h>
#endif

Player::Player() : mInited(false){};
Player::~Player()
{
    dispose();
}

PlayerErrors Player::init()
{
    if (mInited)
        dispose();

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

PlayerErrors Player::loadFile(const std::string &completeFileName, unsigned int &hash)
{
    if (!mInited)
        return backendNotInited;

    hash = 0;
    sounds.push_back(std::make_unique<ActiveSound>());
    sounds.back().get()->completeFileName = std::string(completeFileName);
    hash = sounds.back().get()->soundHash = std::hash<std::string>{}(completeFileName);
    SoLoud::result result = sounds.back().get()->sound.load(completeFileName.c_str());
    if (result != SoLoud::SO_NO_ERROR)
    {
        sounds.emplace_back();
    }
    return (PlayerErrors)result;
}

void Player::pauseSwitch(unsigned int handle)
{
    int handleId;
    ActiveSound *sound = findByHandle(handle, &handleId);
    if (sound == nullptr)
        return;
    soloud.setPause(
        sound->handle[handleId],
        !soloud.getPause(sound->handle[handleId]));
}

bool Player::getPause(unsigned int handle)
{
    int handleId;
    ActiveSound *sound = findByHandle(handle, &handleId);
    if (sound == nullptr)
        return false;
    return soloud.getPause(sound->handle[handleId]);
}

unsigned int Player::play(
    std::size_t soundHash,
    float volume,
    float pan,
    bool paused)
{
    auto const& s = std::find_if(sounds.begin(), sounds.end(),
                [&]( std::unique_ptr<ActiveSound> const& f ) {
                            return (unsigned int)f->soundHash == soundHash; } );

    if (s == sounds.end())
        return 0;

    ActiveSound *sound = s->get();
    SoLoud::handle newHandle = soloud.play(sound->sound, volume, pan, paused, 0);
    sound->handle.emplace_back(newHandle);
    return newHandle;
}

void Player::stop(unsigned int handle)
{
    int handleId;
    ActiveSound *sound = findByHandle(handle, &handleId);
    if (sound == nullptr)
        return;
    soloud.stop(handle);
    // remove the handle from the list
    sound->handle.erase(std::remove_if(sound->handle.begin(), sound->handle.end(),
                                       [handle](SoLoud::handle &f)
                                       { return f == handle; }));
}

void Player::stopSound(std::size_t soundHash)
{
    auto const& s = std::find_if(sounds.begin(), sounds.end(),
                [&]( std::unique_ptr<ActiveSound> const& f ) {
                            return (unsigned int)f->soundHash == soundHash; } );

    if (s == sounds.end())
        return;

    s->get()->sound.stop();
    // remove the sound from the list
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
                          [soundHash](std::unique_ptr<ActiveSound> &f)
                          { return f.get()->soundHash == soundHash; }));
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
        handle = soloud.play(speech);
        sounds.back().get()->handle.push_back(handle);
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

bool Player::isVisualizationEnabled()
{
    return soloud.mFlags & SoLoud::Soloud::ENABLE_VISUALIZATION;
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
double Player::getLength(std::size_t soundHash)
{
    auto const& s = std::find_if(sounds.begin(), sounds.end(),
                [&]( std::unique_ptr<ActiveSound> const& f ) {
                            return (unsigned int)f->soundHash == soundHash; } );
    if (s == sounds.end())
        return 0.0;
    return s->get()->sound.getLength();
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
    // return soloud.getStreamTime(handle);
    return soloud.getStreamPosition(handle);
}

bool Player::getIsValidVoiceHandle(SoLoud::handle handle)
{
    return soloud.isValidVoiceHandle(handle);
}

ActiveSound *Player::findByHandle(SoLoud::handle handle, int *handleId)
{
    *handleId = -1;
    int i = 0;
    // TODO: do a better std pls! \o/ :)
    while (i < (int)sounds.size())
    {
        int index = 0;
        while (index < (int)sounds[i].get()->handle.size())
        {
            if (sounds[i].get()->handle[index] == handle)
            {
                *handleId = index;
                return sounds[i].get();
            }
            ++index;
        }
        ++i;
    }

    return nullptr;
}

void Player::debug()
{
    int n = 0;
    for (auto &sound : sounds)
    {
        printf("%d: \thandle: ", n);
        for (auto &handle : sound.get()->handle)
            printf("%d ", handle);
        printf("  %s\n", sound.get()->completeFileName.c_str());

        n++;
    }
}

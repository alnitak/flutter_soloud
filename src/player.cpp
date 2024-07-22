#include "common.h"
#include "player.h"
#include "soloud.h"
#include "soloud_wav.h"
// #include "soloud_thread.h"
#include "soloud_wavstream.h"
#include "synth/basic_wave.h"

#include <algorithm>
#include <cstdarg>
#include <random>
#ifdef _IS_WIN_
#include <stddef.h> // for size_t
#else
#include <unistd.h>
#endif

Player::Player() : mInited(false), mFilters(&soloud, nullptr) {}

Player::~Player()
{
    dispose();
}

void Player::setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int *))
{
    soloud.setVoiceEndedCallback(voiceEndedCallback);
}

void Player::setStateChangedCallback(void (*stateChangedCallback)(unsigned int))
{
    soloud.setStateChangedCallback(stateChangedCallback);
}

PlayerErrors Player::init(unsigned int sampleRate, unsigned int bufferSize, unsigned int channels)
{
    if (mInited)
        return playerAlreadyInited;

    std::lock_guard<std::mutex> guard(init_deinit_mutex);

    // initialize SoLoud.
    SoLoud::result result = soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF,
        SoLoud::Soloud::MINIAUDIO, sampleRate, bufferSize, channels);
    if (result == SoLoud::SO_NO_ERROR)
        mInited = true;
    else
        result = backendNotInited;
    return (PlayerErrors)result;
}

void Player::dispose()
{
    std::lock_guard<std::mutex> guard(init_deinit_mutex);
    // Clean up SoLoud
    soloud.setVoiceEndedCallback(nullptr);
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
    return (int)sounds.size();
}

const std::string Player::getErrorString(PlayerErrors errorCode) const
{
    switch (errorCode)
    {
    case noError:
        return "No error";
    case invalidParameter:
        return "error: some parameter is invalid!";
    case fileNotFound:
        return "error: file not found!";
    case fileLoadFailed:
        return "error: file found, but could not be loaded!";
    case fileAlreadyLoaded:
        return "error: file already loaded!";
    case dllNotFound:
        return "error: DLL not found, or wrong DLL!";
    case outOfMemory:
        return "error: out of memory!";
    case notImplemented:
        return "error: feature not implemented!";
    case backendNotInited:
        return "error: player not yet initialized!";
    case filterNotFound:
        return "error: filter not found!";
    case unknownError:
        return "error: unknown error!";
    case nullPointer:
        return "error: nullPointer!";
    case soundHashNotFound:
        return "error: sound hash not found!";
    case visualizationNotEnabled:
        return "error: visualization not enabled!";
    case maxNumberOfFiltersReached:
        return "error: max number of filter reached!";
    case filterAlreadyAdded:
        return "error: filter already added!";
    case playerAlreadyInited:
        return "error: the player is already initialized!";
    case soundHandleNotFound:
        return "error: audio handle is not found!";
    }
    return "Other error";
}

PlayerErrors Player::loadFile(
    const std::string &completeFileName,
    bool loadIntoMem,
    unsigned int *hash)
{
    if (!mInited)
        return backendNotInited;

    *hash = 0;

    unsigned int newHash = (unsigned int)std::hash<std::string>{}(completeFileName);
    /// check if the sound has been already loaded
    auto const s = findByHash(newHash);

    if (s != nullptr)
    {
        *hash = newHash;
        return fileAlreadyLoaded;
    }

    sounds.push_back(std::make_shared<ActiveSound>());
    sounds.back().get()->completeFileName = std::string(completeFileName);
    sounds.back().get()->soundHash = newHash;

    SoLoud::result result;
    if (loadIntoMem)
    {
        sounds.back().get()->sound = std::make_shared<SoLoud::Wav>();
        sounds.back().get()->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(sounds.back().get()->sound.get())->load(completeFileName.c_str());
    }
    else
    {
        sounds.back().get()->sound = std::make_shared<SoLoud::WavStream>();
        sounds.back().get()->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(sounds.back().get()->sound.get())->load(completeFileName.c_str());
    }

    if (result != SoLoud::SO_NO_ERROR)
    {
        sounds.pop_back();
    } else {
        sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get());
    }
    *hash = newHash;

    return (PlayerErrors)result;
}

PlayerErrors Player::loadMem(
    const std::string &uniqueName,
    unsigned char *mem,
    int length,
    bool loadIntoMem,
    unsigned int &hash)
{
    if (!mInited)
        return backendNotInited;

    hash = 0;

    unsigned int newHash = (unsigned int)std::hash<std::string>{}(uniqueName);
    /// check if the sound has been already loaded
    auto const s = findByHash(newHash);

    if (s != nullptr)
    {
        hash = newHash;
        return fileAlreadyLoaded;
    }

    sounds.push_back(std::make_shared<ActiveSound>());
    sounds.back().get()->completeFileName = std::string(uniqueName);
    hash = sounds.back().get()->soundHash = newHash;

    SoLoud::result result;
    if (loadIntoMem)
    {
        sounds.back().get()->sound = std::make_shared<SoLoud::Wav>();
        sounds.back().get()->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(sounds.back().get()->sound.get())->loadMem(mem, length, false, true);
    }
    else
    {
        sounds.back().get()->sound = std::make_shared<SoLoud::WavStream>();
        sounds.back().get()->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(sounds.back().get()->sound.get())->loadMem(mem, length, false, true);
    }

    if (result != SoLoud::SO_NO_ERROR)
    {
        sounds.pop_back();
    } else {
        sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get());
    }

    return (PlayerErrors)result;
}

PlayerErrors Player::loadWaveform(
    int waveform,
    bool superWave,
    float scale,
    float detune,
    unsigned int &hash)
{
    if (!mInited)
        return backendNotInited;

    hash = 0;

    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<unsigned int> dist(0, UINT32_MAX);

    hash = dist(g);

    sounds.push_back(std::make_shared<ActiveSound>());
    sounds.back().get()->completeFileName = "";
    sounds.back().get()->soundHash = hash;
    sounds.back().get()->sound = std::make_shared<Basicwave>((SoLoud::Soloud::WAVEFORM)waveform, superWave, detune, scale);
    sounds.back().get()->soundType = TYPE_SYNTH;
    sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get());

    return noError;
}

void Player::setWaveformScale(unsigned int soundHash, float newScale)
{
    auto const s = findByHash(soundHash);

    if (s == nullptr || s->soundType != TYPE_SYNTH)
        return;

    static_cast<Basicwave *>(s->sound.get())->setScale(newScale);
}

void Player::setWaveformDetune(unsigned int soundHash, float newDetune)
{
    auto const s = findByHash(soundHash);

    if (s == nullptr || s->soundType != TYPE_SYNTH)
        return;

    static_cast<Basicwave *>(s->sound.get())->setDetune(newDetune);
}

void Player::setWaveform(unsigned int soundHash, int newWaveform)
{
    auto const s = findByHash(soundHash);

    if (s == nullptr || s->soundType != TYPE_SYNTH)
        return;

    static_cast<Basicwave *>(s->sound.get())->setWaveform((SoLoud::Soloud::WAVEFORM)newWaveform);
}

void Player::setWaveformFreq(unsigned int soundHash, float newFreq)
{
    auto const s = findByHash(soundHash);

    if (s == nullptr || s->soundType != TYPE_SYNTH)
        return;

    static_cast<Basicwave *>(s->sound.get())->setFreq(newFreq);
}

void Player::setWaveformSuperwave(unsigned int soundHash, bool superwave)
{
    auto const s = findByHash(soundHash);

    if (s == nullptr || s->soundType != TYPE_SYNTH)
        return;

    static_cast<Basicwave *>(s->sound.get())->setSuperWave(superwave);
}

void Player::pauseSwitch(unsigned int handle)
{
    setPause(handle, !soloud.getPause(handle));
}

void Player::setPause(unsigned int handle, bool pause)
{
    soloud.setPause(handle, pause);
}

bool Player::getPause(unsigned int handle)
{
    return soloud.getPause(handle);
}

void Player::setRelativePlaySpeed(unsigned int handle, float speed)
{
    if (speed < 0.05)
        speed = 0.05;
    soloud.setRelativePlaySpeed(handle, speed);
}

float Player::getRelativePlaySpeed(unsigned int handle)
{
    return soloud.getRelativePlaySpeed(handle);
}

unsigned int Player::play(
    unsigned int soundHash,
    float volume,
    float pan,
    bool paused,
    bool looping,
    double loopingStartAt)
{
    ActiveSound *sound = findByHash(soundHash);

    if (sound == nullptr)
        return soundHashNotFound;

    SoLoud::handle newHandle = soloud.play(
        *sound->sound.get(), volume, pan, paused, 0);
    if (newHandle != 0)
        sound->handle.push_back(newHandle);

    if (looping)
    {
        setLoopPoint(newHandle, loopingStartAt);
        setLooping(newHandle, true);
    }
    return newHandle;
}

void Player::stop(unsigned int handle)
{
    removeHandle(handle);
    soloud.stop(handle);
}

void Player::removeHandle(unsigned int handle)
{
    const std::lock_guard<std::mutex> lock(lock_mutex);
    // for (auto &sound : sounds)
    //     sound->handle.erase(std::remove_if(
    //         sound->handle.begin(), sound->handle.end(),
    //         [handle](SoLoud::handle &f)
    //         { return f == handle; }));
    bool e = true;
    for (int i = 0; i < sounds.size(); ++i)
        for (int n = 0; n < sounds[i]->handle.size(); ++n)
        {
            if (sounds[i]->handle[n] == handle)
            {
                sounds[i]->handle.erase(sounds[i]->handle.begin() + n);
                e = false;
                break;
            }
            if (e)
                break;
        }
}

void Player::disposeSound(unsigned int soundHash)
{
    ActiveSound *sound = findByHash(soundHash);

    if (sound == nullptr)
        return;

    sound->sound.get()->stop();
    // remove the sound from the list
    sounds.erase(std::remove_if(sounds.begin(), sounds.end(),
                                [soundHash](std::shared_ptr<ActiveSound> &f)
                                { return f.get()->soundHash == soundHash; }));
}

void Player::disposeAllSound()
{
    soloud.stopAll();
    sounds.clear();
}

bool Player::getLooping(unsigned int handle)
{
    return soloud.getLooping(handle);
}

void Player::setLooping(unsigned int handle, bool enable)
{
    soloud.setLooping(handle, enable);
}

double Player::getLoopPoint(unsigned int handle)
{
    return soloud.getLoopPoint(handle);
}

void Player::setLoopPoint(unsigned int handle, double time)
{
    soloud.setLoopPoint(handle, time);
}

PlayerErrors Player::textToSpeech(const std::string &textToSpeech, unsigned int &handle)
{
    if (!mInited)
        return backendNotInited;

    sounds.push_back(std::make_shared<ActiveSound>());
    sounds.back().get()->completeFileName = std::string("");
    SoLoud::result result = speech.setText(textToSpeech.c_str());
    if (result == SoLoud::SO_NO_ERROR)
    {
        handle = soloud.play(speech);
        sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get());
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
double Player::getLength(unsigned int soundHash)
{
    auto const &s = findByHash(soundHash);

    if (s == nullptr || s->soundType == TYPE_SYNTH)
        return 0.0;
    if (s->soundType == TYPE_WAV)
        return static_cast<SoLoud::Wav *>(s->sound.get())->getLength();

    // if (s->soundType == TYPE_WAVSTREAM)
    return static_cast<SoLoud::WavStream *>(s->sound.get())->getLength();
}

// time in seconds
PlayerErrors Player::seek(SoLoud::handle handle, float time)
{
    if (!mInited)
        return backendNotInited;

    ActiveSound *sound = findByHandle(handle);
    if (sound == nullptr || sound->soundType == TYPE_SYNTH)
        return invalidParameter;

    SoLoud::result result = soloud.seek(handle, time);
    return (PlayerErrors)result;
}

// returns time in seconds
double Player::getPosition(SoLoud::handle handle)
{
    return soloud.getStreamPosition(handle);
}

float Player::getGlobalVolume()
{
    return soloud.getGlobalVolume();
}

void Player::setGlobalVolume(float volume)
{
    return soloud.setGlobalVolume(volume);
}

float Player::getVolume(SoLoud::handle handle)
{
    return soloud.getVolume(handle);
}

void Player::setVolume(SoLoud::handle handle, float volume)
{
    return soloud.setVolume(handle, volume);
}

float Player::getPan(SoLoud::handle handle)
{
    return soloud.getPan(handle);
}

void Player::setPan(SoLoud::handle handle, float pan)
{
    if (pan > 1.0f)
        pan = 1.0f;
    if (pan < -1.0f)
        pan = -1.0f;
    soloud.setPan(handle, pan);
}

void Player::setPanAbsolute(SoLoud::handle handle, float panLeft, float panRight)
{
    if (panLeft > 1.0f) panLeft = 1.0f;
    if (panLeft < -1.0f) panLeft = -1.0f;
    if (panRight > 1.0f) panRight = 1.0f;
    if (panRight < -1.0f) panRight = -1.0f;
    soloud.setPanAbsolute(handle, panLeft, panRight);
}

bool Player::isValidHandle(SoLoud::handle handle)
{
    return soloud.isValidVoiceHandle(handle) || soloud.isVoiceGroup(handle);
}

unsigned int Player::getActiveVoiceCount()
{
    return soloud.getActiveVoiceCount();
}

int Player::countAudioSource(unsigned int soundHash)
{
    auto const &s = findByHash(soundHash);

    if (s == nullptr || s->soundType == TYPE_SYNTH)
        return 0;
    if (s->soundType == TYPE_WAV)
    {
        SoLoud::AudioSource *as = static_cast<SoLoud::Wav *>(s->sound.get());
        return soloud.countAudioSource(*as);
    }

    // if (s->soundType == TYPE_WAVSTREAM)
    SoLoud::AudioSource *as = static_cast<SoLoud::WavStream *>(s->sound.get());
    return soloud.countAudioSource(*as);
}

unsigned int Player::getVoiceCount()
{
    return soloud.getVoiceCount();
}

bool Player::getProtectVoice(SoLoud::handle handle)
{
    return soloud.getProtectVoice(handle);
}

void Player::setProtectVoice(SoLoud::handle handle, bool protect)
{
    soloud.setProtectVoice(handle, protect);
}

unsigned int Player::getMaxActiveVoiceCount()
{
    return soloud.getMaxActiveVoiceCount();
}

void Player::setMaxActiveVoiceCount(unsigned int maxVoiceCount)
{
    soloud.setMaxActiveVoiceCount(maxVoiceCount);
}

ActiveSound *Player::findByHandle(SoLoud::handle handle)
{
    int i = 0;
    // TODO(marco): do a better std pls! \o/
    while (i < (int)sounds.size())
    {
        int index = 0;
        while (index < (int)sounds[i].get()->handle.size())
        {
            if (sounds[i].get()->handle[index] == handle)
            {
                return sounds[i].get();
            }
            ++index;
        }
        ++i;
    }

    return nullptr;
}

ActiveSound *Player::findByHash(unsigned int soundHash)
{
    auto const &s = std::find_if(sounds.begin(), sounds.end(),
                                 [&](std::shared_ptr<ActiveSound> const &f)
                                 { return f->soundHash == soundHash; });
    if (s == sounds.end())
        return nullptr;

    return s->get();
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

/////////////////////////////////////////
/// voice groups
/////////////////////////////////////////

unsigned int Player::createVoiceGroup() {
    auto ret = soloud.createVoiceGroup();
    return ret;
}

void Player::destroyVoiceGroup(SoLoud::handle handle) {
    soloud.destroyVoiceGroup(handle);
}

void Player::addVoiceToGroup(SoLoud::handle voiceGroupHandle, SoLoud::handle voiceHandle) {
    soloud.addVoiceToGroup(voiceGroupHandle, voiceHandle);
}

bool Player::isVoiceGroup(SoLoud::handle handle) {
    return soloud.isVoiceGroup(handle);
}

bool Player::isVoiceGroupEmpty(SoLoud::handle handle) {
    return soloud.isVoiceGroupEmpty(handle);
}

/////////////////////////////////////////
/// faders
/////////////////////////////////////////

void Player::fadeGlobalVolume(float to, float time)
{
    soloud.fadeGlobalVolume(to, time);
}

void Player::fadeVolume(SoLoud::handle handle, float to, float time)
{
    soloud.fadeVolume(handle, to, time);
}

void Player::fadePan(SoLoud::handle handle, float to, float time)
{
    soloud.fadePan(handle, to, time);
}

void Player::fadeRelativePlaySpeed(SoLoud::handle handle, float to, float time)
{
    soloud.fadeRelativePlaySpeed(handle, to, time);
}

void Player::schedulePause(SoLoud::handle handle, float time)
{
    soloud.schedulePause(handle, time);
}

void Player::scheduleStop(SoLoud::handle handle, float time)
{
    soloud.scheduleStop(handle, time);
}

void Player::oscillateVolume(SoLoud::handle handle, float from, float to, float time)
{
    soloud.oscillateVolume(handle, from, to, time);
}

void Player::oscillatePan(SoLoud::handle handle, float from, float to, float time)
{
    soloud.oscillatePan(handle, from, to, time);
}

void Player::oscillateRelativePlaySpeed(SoLoud::handle handle, float from, float to, float time)
{
    soloud.oscillateRelativePlaySpeed(handle, from, to, time);
}

void Player::oscillateGlobalVolume(float from, float to, float time)
{
    soloud.oscillateGlobalVolume(from, to, time);
}

/////////////////////////////////////////
/// 3D audio methods
/////////////////////////////////////////

void Player::update3dAudio()
{
    soloud.update3dAudio();
}

unsigned int Player::play3d(
    unsigned int soundHash,
    float posX,
    float posY,
    float posZ,
    float velX,
    float velY,
    float velZ,
    float volume,
    bool paused,
    unsigned int bus,
    bool looping,
    double loopingStartAt)
{
    ActiveSound *sound = findByHash(soundHash);
    if (sound == 0)
        return soundHashNotFound;

    SoLoud::handle newHandle = soloud.play3d(
        *sound->sound.get(),
        posX, posY, posZ,
        velX, velY, velZ,
        volume,
        paused,
        bus);
    if (newHandle != 0)
        sound->handle.emplace_back(newHandle);
    if (looping)
    {
        seek(newHandle, loopingStartAt);
        setLoopPoint(newHandle, loopingStartAt);
        setLooping(newHandle, true);
        setPause(newHandle, paused);
    }
    return newHandle;
}

void Player::set3dSoundSpeed(float speed)
{
    soloud.set3dSoundSpeed(speed);
}

float Player::get3dSoundSpeed()
{
    return soloud.get3dSoundSpeed();
}

void Player::set3dListenerParameters(
    float aPosX, float aPosY, float aPosZ,
    float aAtX, float aAtY, float aAtZ,
    float aUpX, float aUpY, float aUpZ,
    float aVelocityX, float aVelocityY, float aVelocityZ)
{
    soloud.set3dListenerParameters(
        aPosX, aPosY, aPosZ,
        aAtX, aAtY, aAtZ,
        aUpX, aUpY, aUpZ,
        aVelocityX, aVelocityY, aVelocityZ);
}

void Player::set3dListenerPosition(float aPosX,
                                   float aPosY,
                                   float aPosZ)
{
    soloud.set3dListenerPosition(aPosX, aPosY, aPosZ);
}

void Player::set3dListenerAt(float aAtX,
                             float aAtY,
                             float aAtZ)
{
    soloud.set3dListenerAt(aAtX, aAtY, aAtZ);
}

void Player::set3dListenerUp(float aUpX,
                             float aUpY,
                             float aUpZ)
{
    soloud.set3dListenerAt(aUpX, aUpY, aUpZ);
}

void Player::set3dListenerVelocity(float aVelocityX,
                                   float aVelocityY,
                                   float aVelocityZ)
{
    soloud.set3dListenerVelocity(aVelocityX, aVelocityY, aVelocityZ);
}

void Player::set3dSourceParameters(
    unsigned int aVoiceHandle,
    float aPosX, float aPosY, float aPosZ,
    float aVelocityX, float aVelocityY, float aVelocityZ)
{
    soloud.set3dSourceParameters(aVoiceHandle,
                                 aPosX, aPosY, aPosZ,
                                 aVelocityX, aVelocityY, aVelocityZ);
}

void Player::set3dSourcePosition(
    unsigned int aVoiceHandle,
    float aPosX,
    float aPosY,
    float aPosZ)
{
    soloud.set3dSourcePosition(aVoiceHandle, aPosX, aPosY, aPosZ);
}

void Player::set3dSourceVelocity(
    unsigned int aVoiceHandle,
    float aVelocityX,
    float aVelocityY,
    float aVelocityZ)
{
    soloud.set3dSourceVelocity(aVoiceHandle, aVelocityX, aVelocityY, aVelocityZ);
}

void Player::set3dSourceMinMaxDistance(
    unsigned int aVoiceHandle,
    float aMinDistance,
    float aMaxDistance)
{
    soloud.set3dSourceMinMaxDistance(aVoiceHandle, aMinDistance, aMaxDistance);
}

void Player::set3dSourceAttenuation(
    unsigned int aVoiceHandle,
    unsigned int aAttenuationModel,
    float aAttenuationRolloffFactor)
{
    soloud.set3dSourceAttenuation(aVoiceHandle, aAttenuationModel, aAttenuationRolloffFactor);
}

void Player::set3dSourceDopplerFactor(
    unsigned int aVoiceHandle,
    float aDopplerFactor)
{
    soloud.set3dSourceDopplerFactor(aVoiceHandle, aDopplerFactor);
}
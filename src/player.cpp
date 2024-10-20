#include "common.h"
#include "player.h"
#include "soloud.h"
#include "soloud_wav.h"
// #include "soloud_thread.h"
#include "soloud_wavstream.h"
#include "synth/basic_wave.h"

#include <algorithm>
#include <cstdarg>
#include <cstring>
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

PlayerErrors Player::init(unsigned int sampleRate, unsigned int bufferSize, unsigned int channels, int deviceID)
{
    if (mInited)
        return playerAlreadyInited;

    void *playbackInfos_id = nullptr;
    if (deviceID != -1)
    {
        // Calling this will init [pPlaybackInfos]
        auto const devices = listPlaybackDevices();
        if (devices.size() == 0 || deviceID >= devices.size())
            return backendNotInited;
        playbackInfos_id = &pPlaybackInfos[deviceID].id;
    }

    // initialize SoLoud.
    SoLoud::result result = soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF,
        SoLoud::Soloud::MINIAUDIO, sampleRate, bufferSize, channels, playbackInfos_id);
    if (result == SoLoud::SO_NO_ERROR)
    {
        mInited = true;
        mSampleRate = sampleRate;
        mBufferSize = bufferSize;
        mChannels = channels;
    }
    else
        result = backendNotInited;
    return (PlayerErrors)result;
}

PlayerErrors Player::changeDevice(int deviceID)
{
    if (!mInited)
        return backendNotInited;

    void *playbackInfos_id = nullptr;

    // Calling this will init [pPlaybackInfos]
    auto const devices = listPlaybackDevices();
    if (devices.size() == 0 || deviceID >= devices.size())
        return backendNotInited;
    playbackInfos_id = &pPlaybackInfos[deviceID].id;

    SoLoud::result result = soloud.miniaudio_changeDevice(playbackInfos_id);

    if (result != SoLoud::SO_NO_ERROR)
        result = backendNotInited;
    return noError;
}

// List available playback devices.
std::vector<PlaybackDevice> Player::listPlaybackDevices()
{
    // printf("***************** LIST DEVICES START\n");
    ma_context context;
    ma_uint32 playbackCount;
    ma_device_info *pCaptureInfos;
    ma_uint32 captureCount;
    std::vector<PlaybackDevice> ret;
    ma_result result;
    if ((result = ma_context_init(NULL, 0, NULL, &context)) != MA_SUCCESS)
    {
        printf("Failed to initialize context %d\n", result);
        return ret;
    }

    if ((result = ma_context_get_devices(
             &context,
             &pPlaybackInfos,
             &playbackCount,
             &pCaptureInfos,
             &captureCount)) != MA_SUCCESS)
    {
        printf("Failed to get devices %d\n", result);
        return ret;
    }

    // Loop over each device info and do something with it. Here we just print
    // the name with their index. You may want
    // to give the user the opportunity to choose which device they'd prefer.
    for (ma_uint32 i = 0; i < playbackCount; i++)
    {
        // printf("######%s %d - %s\n",
        //        pPlaybackInfos[i].isDefault ? " X" : "-",
        //        i,
        //        pPlaybackInfos[i].name);
        PlaybackDevice cd;
        cd.name = strdup(pPlaybackInfos[i].name);
        cd.isDefault = pPlaybackInfos[i].isDefault;
        cd.id = i;
        ret.push_back(cd);
    }
    // printf("***************** LIST DEVICES END\n");
    return ret;
}

void Player::dispose()
{
    // Clean up SoLoud
    setVoiceEndedCallback(nullptr);
    setStateChangedCallback(nullptr);
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
    case filterParameterGetError:
        return "error: getting filter parameter error!";
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

    unsigned int newHash = (int32_t)std::hash<std::string>{}(completeFileName) & 0x7fffffff;
    /// check if the sound has been already loaded
    auto const s = findByHash(newHash);

    if (s != nullptr)
    {
        *hash = newHash;
        return fileAlreadyLoaded;
    }

    auto sound = std::make_unique<ActiveSound>();

    sound->completeFileName = std::string(completeFileName);
    sound->soundHash = newHash;

    SoLoud::result result;
    if (loadIntoMem)
    {
        sound->sound = std::make_unique<SoLoud::Wav>();
        sound->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(sound->sound.get())->load(completeFileName.c_str());
    }
    else
    {
        sound->sound = std::make_unique<SoLoud::WavStream>();
        sound->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(sound->sound.get())->load(completeFileName.c_str());
    }

    if (result != SoLoud::SO_NO_ERROR)
    {
        *hash = 0;
    }
    else
    {
        *hash = newHash;
        sound->filters = std::make_unique<Filters>(&soloud, sound.get());
        sounds.push_back(std::move(sound));
    }

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

    unsigned int newHash = (int32_t)std::hash<std::string>{}(uniqueName) & 0x7fffffff;
    /// check if the sound has been already loaded
    auto const s = findByHash(newHash);

    if (s != nullptr)
    {
        hash = newHash;
        return fileAlreadyLoaded;
    }

    std::unique_ptr<ActiveSound> newSound = std::make_unique<ActiveSound>();
    newSound.get()->completeFileName = std::string(uniqueName);
    hash = newHash;
    newSound.get()->soundHash = newHash;
    SoLoud::result result;
    if (loadIntoMem)
    {
        newSound.get()->sound = std::make_unique<SoLoud::Wav>();
        newSound.get()->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(newSound.get()->sound.get())->loadMem(mem, length, false, true);
    }
    else
    {
        newSound.get()->sound = std::make_unique<SoLoud::WavStream>();
        newSound.get()->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(newSound.get()->sound.get())->loadMem(mem, length, false, true);
    }

    if (result == SoLoud::SO_NO_ERROR)
    {
        newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get());
        sounds.push_back(std::move(newSound));
    }

    return (PlayerErrors)result;
}

PlayerErrors Player::setBufferStream(
    const std::string &uniqueName,
    unsigned int &hash,
    unsigned long maxBufferSize,
    SoLoud::PCMformat pcmFormat)
{
    if (!mInited)
        return backendNotInited;

    hash = 0;

    unsigned int newHash = (int32_t)std::hash<std::string>{}(uniqueName) & 0x7fffffff;
    /// check if the sound has been already loaded
    auto const s = findByHash(newHash);

    if (s != nullptr)
    {
        hash = newHash;
        return fileAlreadyLoaded;
    }

    std::unique_ptr<ActiveSound> newSound = std::make_unique<ActiveSound>();
    newSound.get()->completeFileName = std::string(uniqueName);
    hash = newHash;
    newSound.get()->soundHash = newHash;

    newSound.get()->sound = std::make_unique<SoLoud::BufferStream>();
    newSound.get()->soundType = TYPE_BUFFER_STREAM;
    static_cast<SoLoud::BufferStream *>(newSound.get()->sound.get())->setBufferStream(
        maxBufferSize, false, true, pcmFormat);

    newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get());
    sounds.push_back(std::move(newSound));

    return noError;
}

PlayerErrors Player::addAudioDataStream(
    unsigned int hash,
    const unsigned char *data,
    unsigned int aDataLen)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != TYPE_BUFFER_STREAM)
        return soundHashNotFound;

    return (PlayerErrors)static_cast<SoLoud::BufferStream *>(s->sound.get())->addData(data, aDataLen);
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

    sounds.push_back(std::make_unique<ActiveSound>());
    sounds.back().get()->completeFileName = "";
    sounds.back().get()->soundHash = hash;
    sounds.back().get()->sound = std::make_unique<Basicwave>((SoLoud::Soloud::WAVEFORM)waveform, superWave, detune, scale);
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
    const std::lock_guard<std::mutex> guard(remove_handle_mutex);
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
                                [soundHash](std::unique_ptr<ActiveSound> &f)
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

    sounds.push_back(std::make_unique<ActiveSound>());
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
    if (panLeft > 1.0f)
        panLeft = 1.0f;
    if (panLeft < -1.0f)
        panLeft = -1.0f;
    if (panRight > 1.0f)
        panRight = 1.0f;
    if (panRight < -1.0f)
        panRight = -1.0f;
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
                                 [&](std::unique_ptr<ActiveSound> const &f)
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

unsigned int Player::createVoiceGroup()
{
    auto ret = soloud.createVoiceGroup();
    return ret;
}

void Player::destroyVoiceGroup(SoLoud::handle handle)
{
    soloud.destroyVoiceGroup(handle);
}

void Player::addVoiceToGroup(SoLoud::handle voiceGroupHandle, SoLoud::handle voiceHandle)
{
    soloud.addVoiceToGroup(voiceGroupHandle, voiceHandle);
}

bool Player::isVoiceGroup(SoLoud::handle handle)
{
    return soloud.isVoiceGroup(handle);
}

bool Player::isVoiceGroupEmpty(SoLoud::handle handle)
{
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
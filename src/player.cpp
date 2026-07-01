#include "soloud_common.h"
#include "player.h"
#include "filters/filters.h"
#include "soloud.h"
#include "soloud/include/soloud.h"
#include "soloud_wav.h"
// #include "soloud_thread.h"
#include "soloud_wavstream.h"
#include "synth/basic_wave.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <random>
#include <thread>

#ifdef _IS_WIN_
#include <stddef.h> // for size_t
#else
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#define __WEB__ 1
#else
#define __WEB__ 0
#endif

namespace
{
    constexpr unsigned int kOggXiphBufferStreamMaxBytes = 512u * 1024u * 1024u;

    bool readFileBytes(const std::string &filePath,
                       std::vector<unsigned char> &bytes)
    {
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.good())
        {
            return false;
        }

        const std::streamoff fileSize = file.tellg();
        if (fileSize <= 0)
        {
            return false;
        }

        bytes.resize(static_cast<size_t>(fileSize));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(bytes.data()), fileSize);
        return file.gcount() == fileSize;
    }

    bool isOggXiphBytes(const std::vector<unsigned char> &bytes)
    {
        if (bytes.size() < 35 || std::memcmp(bytes.data(), "OggS", 4) != 0)
        {
            return false;
        }

        size_t scanOffset = 0;
        const size_t scanLimit = std::min(bytes.size(), static_cast<size_t>(64 * 1024));
        while (scanOffset + 27 < scanLimit)
        {
            if (std::memcmp(bytes.data() + scanOffset, "OggS", 4) != 0)
            {
                ++scanOffset;
                continue;
            }

            const uint8_t segmentCount = bytes[scanOffset + 26];
            const size_t segmentTableOffset = scanOffset + 27;
            if (segmentTableOffset + segmentCount > scanLimit)
            {
                return false;
            }

            size_t payloadSize = 0;
            for (uint8_t i = 0; i < segmentCount; ++i)
            {
                payloadSize += bytes[segmentTableOffset + i];
            }

            const size_t payloadOffset = segmentTableOffset + segmentCount;
            if (payloadOffset + payloadSize > bytes.size())
            {
                return false;
            }

            if (payloadSize >= 8 &&
                std::memcmp(bytes.data() + payloadOffset, "OpusHead", 8) == 0)
            {
                return true;
            }

            if (payloadSize >= 13 &&
                std::memcmp(bytes.data() + payloadOffset + 1, "FLAC", 4) == 0 &&
                std::memcmp(bytes.data() + payloadOffset + 9, "fLaC", 4) == 0)
            {
                return true;
            }

            scanOffset = payloadOffset + payloadSize;
        }

        return false;
    }

    PlayerErrors loadOggXiphBufferStream(Player *player,
                                         ActiveSound *activeSound,
                                         const std::vector<unsigned char> &bytes)
    {
        if (player == nullptr || activeSound == nullptr || bytes.empty())
        {
            return invalidParameter;
        }

        activeSound->sound = std::make_unique<SoLoud::BufferStream>();
        activeSound->soundType = TYPE_BUFFER_STREAM;
        PCMformat pcmFormat = {player->mSampleRate, player->mChannels, 4, AUTO};
        auto *bufferStream =
            static_cast<SoLoud::BufferStream *>(activeSound->sound.get());
        PlayerErrors error = bufferStream->setBufferStream(
            player,
            activeSound,
            kOggXiphBufferStreamMaxBytes,
            BufferingType::PRESERVED,
            0.0f,
            pcmFormat);
        if (error != noError)
        {
            return error;
        }

        error = bufferStream->addData(bytes.data(),
                                      static_cast<unsigned int>(bytes.size()));
        if (error != noError)
        {
            return error;
        }

        bufferStream->setDataIsEnded();
        return noError;
    }
}

Player::Player() : mInited(false), mFilters(&soloud, nullptr, nullptr),
                   mPauseRequested(false), mStopPauseThread(false),
                   mPauseThreadRunning(false)
{
}

Player::~Player()
{
    // If the scheduler was started, stop it before touching Soloud.
    stopPauseEngineScheduler();

    if (!mInited)
    {
        // dispose() was called properly — Soloud is already deinited and safe.
        // Let ~Soloud() run normally to free its remaining allocations.
        return;
    }

    // Neutralize the Soloud member so ~Soloud() and its deinit() call become
    // harmless no-ops. The OS will reclaim all resources on process exit.
    //
    // We intentionally leak here — this only runs during abnormal exit
    // (app closed without calling dispose()), and the process is terminating.
    soloud.mBackendCleanupFunc = nullptr;
    soloud.mAudioThreadMutex = nullptr;
    soloud.mHighestVoice = 0;
    soloud.mVoiceGroup = nullptr;
    soloud.mVoiceGroupCount = 0;
    soloud.mResampleData = nullptr;
    soloud.mResampleDataOwner = nullptr;
    for (int i = 0; i < FILTERS_PER_STREAM; i++)
    {
        soloud.mFilterInstance[i] = nullptr;
    }
}

void Player::dispose()
{
    if (!mInited)
        return;

    // Stop accepting new pause requests and wake the scheduler so it exits.
    stopPauseEngineScheduler();

    mInited = false;

    // Clean up SoLoud
    setVoiceEndedCallback(nullptr);
    setStateChangedCallback(nullptr);
    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        sounds.clear();
    }
    soloud.deinit();
}

void Player::setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int *))
{
    soloud.setVoiceEndedCallback(voiceEndedCallback);
}

void Player::setStateChangedCallback(void (*stateChangedCallback)(unsigned int))
{
    soloud.setStateChangedCallback(stateChangedCallback);
}

// Defined in the miniaudio backend (soloud_miniaudio.cpp). Forward-declared
// here so we don't need to pull in the backend-internal header.
namespace SoLoud { void miniaudio_setLowLatency(bool aLowLatency); }

PlayerErrors Player::init(unsigned int sampleRate, unsigned int bufferSize, unsigned int channels, int deviceID, bool lowLatency)
{
    if (mInited)
        return playerAlreadyInited;

    // Choose the device performance profile before SoLoud opens the backend.
    SoLoud::miniaudio_setLowLatency(lowLatency);

    void *playbackInfos_id = nullptr;
    if (deviceID != -1)
    {
        // Get the device list and find the requested device
        auto const devices = listPlaybackDevices();
        if (devices.size() == 0 || deviceID >= devices.size())
            return noPlaybackDevicesFound;
        // Use the stored device ID from the PlaybackDevice struct
        playbackInfos_id = (void *)&devices[deviceID].deviceId;
    }

    // initialize SoLoud.
    SoLoud::result result;
    result = soloud.init(0, SoLoud::Soloud::MINIAUDIO, sampleRate, bufferSize, channels, playbackInfos_id);
    if (result != SoLoud::SO_NO_ERROR)
    {
        return backendNotInited;
    }
    else
    {
        soloud.setPostClipScaler(1.0f);
        mInited = true;
        mSampleRate = sampleRate;
        mBufferSize = bufferSize;
        mChannels = channels;
        // Start the deferred-pause scheduler now that the engine is in use.
        startPauseEngineScheduler();
    }
    return (PlayerErrors)result;
}

PlayerErrors Player::changeDevice(int deviceID)
{
    if (!mInited)
        return backendNotInited;

    // Get the device list and find the requested device
    auto const devices = listPlaybackDevices();
    if (devices.size() == 0 || deviceID >= devices.size())
        return noPlaybackDevicesFound;

    // Use the stored device ID from the PlaybackDevice struct
    void *playbackInfos_id = (void *)&devices[deviceID].deviceId;

    SoLoud::result result = soloud.miniaudio_changeDevice(playbackInfos_id);

    // miniaudio_changeDevice can only throw UNKNOWN_ERROR. This means that
    // for some reasons the device could not be changed (maybe the engine
    // was turned off in the meantime?).
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
        // Failed to initialize audio context.
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
        ma_context_uninit(&context);
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
        cd.deviceId = pPlaybackInfos[i].id; // Copy the device ID
        ret.push_back(cd);
    }
    // printf("***************** LIST DEVICES END\n");
    ma_context_uninit(&context);
    return ret;
}

bool Player::isInited()
{
    return mInited;
}

int Player::getSoundsCount()
{
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
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
    case noPlaybackDevicesFound:
        return "error: no playback devices found!";
    case pcmBufferFull:
        return "error: pcm buffer full!";
    case hashIsNotABufferStream:
        return "error: hash is not a buffer stream!";
    case streamEndedAlready:
        return "error: trying to add PCM data but the stream is marked to be ended!";
    case failedToCreateOpusDecoder:
        return "error: failed to create Opus decoder!";
    case failedToDecodeOpusPacket:
        return "error: failed to decode Opus packet!";
    case bufferStreamCanBePlayedOnlyOnce:
        return "error: buffer stream can be played only once!";
    case maxActiveVoiceCountReached:
        return "error: the maximum number of active voices has been reached!";
    case wrongBufferTypeToAskForTimeConsumed:
        return "error: trying to get time consumed from wrong buffer type!";
    case bufferStreamWithReleasedBufferTypeCannotBeSeeked:
        return "error: buffer stream with released buffer type cannot be seeked!";
    case audioFormatNotSupported:
        return "error: audio format not supported!";
    case xiphLibsNotFound:
        return "error: Xiph libraries not found!";
    case busIdNotFound:
        return "error: bus id not found!";
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
    /// check if the sound has already been loaded
    auto const s = findByHash(newHash);

    // If the hash already exists, create a new unique random hash.
    // This allows loading the same file multiple times with unique identifiers.
    if (s != nullptr)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::uniform_int_distribution<unsigned int> dist(0, 0x7fffffff);
        do
        {
            newHash = dist(g);
        } while (findByHash(newHash) != nullptr);
    }

    std::unique_ptr<ActiveSound> newSound = std::make_unique<ActiveSound>();
    newSound.get()->completeFileName = std::string(completeFileName);
    *hash = newHash;
    newSound.get()->soundHash = newHash;

    SoLoud::result result;
    // This function is never called when running on the Web, but [__WEB__] is checked for consistency with [loadMem].
    if (loadIntoMem || __WEB__)
    {
        newSound.get()->sound = std::make_unique<SoLoud::Wav>();
        newSound.get()->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(newSound.get()->sound.get())->load(completeFileName.c_str());
    }
    else
    {
        newSound.get()->sound = std::make_unique<SoLoud::WavStream>();
        newSound.get()->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(newSound.get()->sound.get())->load(completeFileName.c_str());
    }

    PlayerErrors loadError = static_cast<PlayerErrors>(result);
    if (result != SoLoud::SO_NO_ERROR)
    {
        std::vector<unsigned char> bytes;
        if (readFileBytes(completeFileName, bytes) && isOggXiphBytes(bytes))
        {
            loadError = loadOggXiphBufferStream(this, newSound.get(), bytes);
        }
    }

    if (loadError != noError)
    {
        *hash = 0;
    }
    else
    {
        *hash = newHash;
        newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get(), nullptr);
        {
            std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
            sounds.push_back(std::move(newSound));
        }
    }

    // Return fileAlreadyLoaded if the filename hash was already in use,
    // even though we've now loaded a new instance with a unique hash.
    if (s != nullptr && loadError == noError)
    {
        return fileAlreadyLoaded;
    }

    return loadError;
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
    /// check if the sound has already been loaded
    auto const s = findByHash(newHash);

    // If already loaded, generate a unique hash
    if (s != nullptr)
    {
        std::random_device rd;
        std::mt19937 g(rd());
        std::uniform_int_distribution<unsigned int> dist(0, 0x7fffffff);
        do
        {
            newHash = dist(g);
        } while (findByHash(newHash) != nullptr);
    }

    auto newSound = std::make_unique<ActiveSound>();
    newSound.get()->completeFileName = std::string(uniqueName);
    hash = newHash;
    newSound.get()->soundHash = newHash;
    SoLoud::result result;
    if (loadIntoMem || __WEB__)
    {
        newSound.get()->sound = std::make_unique<SoLoud::Wav>();
        newSound.get()->soundType = TYPE_WAV;
        result = static_cast<SoLoud::Wav *>(newSound.get()->sound.get())->loadMem(mem, length, true, true);
    }
    else
    {
        newSound.get()->sound = std::make_unique<SoLoud::WavStream>();
        newSound.get()->soundType = TYPE_WAVSTREAM;
        result = static_cast<SoLoud::WavStream *>(newSound.get()->sound.get())->loadMem(mem, length, false, true);
    }

    PlayerErrors loadError = static_cast<PlayerErrors>(result);
    if (result != SoLoud::SO_NO_ERROR && mem != nullptr && length > 0)
    {
        std::vector<unsigned char> bytes(mem, mem + length);
        if (isOggXiphBytes(bytes))
        {
            loadError = loadOggXiphBufferStream(this, newSound.get(), bytes);
        }
    }

    if (loadError == noError)
    {
        newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get(), nullptr);
        {
            std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
            sounds.push_back(std::move(newSound));
        }
    }

    // Return fileAlreadyLoaded if the unique name hash was already in use,
    // even though we've now loaded a new instance with a unique hash.
    if (s != nullptr && loadError == noError)
    {
        return fileAlreadyLoaded;
    }

    return loadError;
}

PlayerErrors Player::setBufferStream(
    unsigned int &hash,
    unsigned long maxBufferSize,
    BufferingType bufferingType,
    SoLoud::time bufferingTimeNeeds,
    PCMformat pcmFormat,
    dartOnBufferingCallback_t onBufferingCallback,
    dartOnMetadataCallback_t onMetadataCallback)
{
    if (!mInited)
        return backendNotInited;

    std::random_device rd;
    std::mt19937 g(rd());
    std::uniform_int_distribution<unsigned int> dist(0, INT32_MAX);

    hash = dist(g);

    auto newSound = std::make_unique<ActiveSound>();
    newSound.get()->completeFileName = "";
    newSound.get()->soundHash = hash;

    newSound.get()->sound = std::make_unique<SoLoud::BufferStream>();

    newSound.get()->soundType = SoundType::TYPE_BUFFER_STREAM;
    PlayerErrors e = static_cast<SoLoud::BufferStream *>(newSound.get()->sound.get())->setBufferStream(this, newSound.get(), static_cast<unsigned int>(maxBufferSize), bufferingType, bufferingTimeNeeds, pcmFormat, onBufferingCallback, onMetadataCallback);

    newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get(), nullptr);
    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        sounds.push_back(std::move(newSound));
    }

    return e;
}

PlayerErrors Player::addAudioDataStream(
    unsigned int hash,
    const unsigned char *data,
    unsigned int aDataLen)
{
    auto const s = findByHash(hash);

    if (s == nullptr)
        return PlayerErrors::soundHashNotFound;

    if (s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return hashIsNotABufferStream;

    return static_cast<SoLoud::BufferStream *>(s->sound.get())->addData(data, aDataLen, false);
}

PlayerErrors Player::resetBufferStream(unsigned int hash)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
    {
        return PlayerErrors::soundHashNotFound;
    }

    static_cast<SoLoud::BufferStream *>(s->sound.get())->resetBuffer();
    return PlayerErrors::noError;
}

PlayerErrors Player::setBufferIcyMetaInt(unsigned int hash, int icyMetaInt)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
    {
        return PlayerErrors::soundHashNotFound;
    }

    static_cast<SoLoud::BufferStream *>(s->sound.get())->setBufferIcyMetaInt(icyMetaInt);
    return PlayerErrors::noError;
}

PlayerErrors Player::getStreamTimeConsumed(unsigned int hash, float *timeConsumed)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    if (static_cast<SoLoud::BufferStream *>(s->sound.get())->getBufferingType() != BufferingType::RELEASED)
        return PlayerErrors::wrongBufferTypeToAskForTimeConsumed;

    *timeConsumed = static_cast<SoLoud::BufferStream *>(s->sound.get())->getStreamTimeConsumed();
    return PlayerErrors::noError;
}

PlayerErrors Player::setDataIsEnded(unsigned int hash)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    static_cast<SoLoud::BufferStream *>(s->sound.get())->setDataIsEnded();
    return PlayerErrors::noError;
}

PlayerErrors Player::getBufferSize(unsigned int hash, unsigned int *sizeInBytes)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    auto *bufferStream = static_cast<SoLoud::BufferStream *>(s->sound.get());
    std::lock_guard<std::recursive_mutex> lock(bufferStream->mBuffer.bufferMutex);
    *sizeInBytes = static_cast<unsigned int>(
        bufferStream->mBuffer.getActiveSizeInBytes() +
        bufferStream->buffer.size());
    return PlayerErrors::noError;
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
    std::uniform_int_distribution<unsigned int> dist(0, INT32_MAX);

    hash = dist(g);

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        sounds.push_back(std::make_unique<ActiveSound>());
        sounds.back().get()->completeFileName = "";
        sounds.back().get()->soundHash = hash;
        sounds.back().get()->sound = std::make_unique<Basicwave>((SoLoud::Soloud::WAVEFORM)waveform, superWave, detune, scale);
        sounds.back().get()->soundType = TYPE_SYNTH;
        sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get(), nullptr);
    }

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

void Player::setPause(unsigned int handle, bool pause, bool isUserAction)
{
    if (!pause)
    {
        // When unpausing, ensure the audio device is started.
        // This handles cases where the OS stopped the device without notifying us
        // (e.g., Control Center pause on iOS).
        soloud.resume();
    }

    soloud.setPause(handle, pause);

    // Track whether this handle was paused by the user, so the BufferStream
    // buffering logic does not automatically unpause it when data becomes
    // available. The user must explicitly unpause it again.
    auto s = findByHandle(handle);
    if (s != nullptr)
    {
        for (size_t i = 0; i < s->handle.size(); i++)
        {
            if (s->handle[i].handle == handle)
            {
                s->handle[i].isUserPaused = pause && isUserAction;
                break;
            }
        }
    }

    if (pause)
    {
        // When pausing, check if there are any remaining active voices.
        // If no voices are active, pause the audio device to allow the OS
        // to properly manage the audio session (important for Control Center
        // and remote command handling on iOS).
        pauseEngine();
    }
}

// On some platforms (notably iOS) the OS can take a short time to fully
// tear down or hand back the audio session after the last active voice is
// stopped/paused. If we pause the SoLoud engine immediately, a subsequent
// play/resume request can arrive while the audio device is still settling,
// which can cause the OS to keep the Control Center / lock-screen media
// controls in an inconsistent state or to fail to restart playback cleanly.
//
// To avoid this, we defer the engine pause by ~500 ms. This gives the audio
// backend and the OS enough time to stabilize, while still pausing the
// engine promptly once no voices remain active. It also coalesces rapid
// stop/pause events so we don't pause/unpause the device repeatedly. The
// latter happens when stopping many sounds in a short time and new sounds
// are then started causing a lag when starting to play again.
//
// Instead of spawning a detached thread for every request, a single
// persistent scheduler thread handles all pause requests.
void Player::pauseEngine()
{
#ifdef __EMSCRIPTEN__
    // Web: the wasm build is single-threaded (no pthreads), so the deferred
    // scheduler thread cannot run. Pause the device immediately instead. The
    // browser's AudioContext does not have the OS audio-session settling issue
    // that motivates the delay on native platforms.
    if (mInited && soloud.getActiveVoiceCount() == 0)
        soloud.pause();
#else
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);
        if (!mPauseThreadRunning)
            return;
        mPauseRequested = true;
    }
    mPauseCv.notify_one();
#endif
}

void Player::startPauseEngineScheduler()
{
#ifndef __EMSCRIPTEN__
    std::lock_guard<std::mutex> lock(mPauseMutex);
    if (mPauseThreadRunning)
        return;
    mStopPauseThread = false;
    mPauseRequested = false;
    mPauseThread = std::thread(&Player::pauseEngineScheduler, this);
    mPauseThreadRunning = true;
#endif
}

void Player::stopPauseEngineScheduler()
{
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);
        if (!mPauseThreadRunning)
            return;
        mStopPauseThread = true;
    }
    mPauseCv.notify_all();
    if (mPauseThread.joinable())
    {
        mPauseThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);
        mPauseThreadRunning = false;
    }
#endif
}

void Player::pauseEngineScheduler()
{
    while (!mStopPauseThread)
    {
        std::unique_lock<std::mutex> lock(mPauseMutex);
        mPauseCv.wait(lock, [this]
                      { return mPauseRequested || mStopPauseThread; });
        if (mStopPauseThread)
            break;

        // A request arrived. Reset it and wait for the delay, but wake early
        // if another request arrives (coalescing rapid calls).
        mPauseRequested = false;
        mPauseCv.wait_for(lock, std::chrono::milliseconds(kPauseEngineDelayMs),
                          [this]
                          { return mPauseRequested || mStopPauseThread; });

        if (mStopPauseThread)
            break;

        // If another request arrived during the wait, loop back and restart
        // the delay so the pause happens only after the burst of requests ends.
        if (mPauseRequested)
            continue;

        lock.unlock();
        if (mInited && soloud.getActiveVoiceCount() == 0)
        {
            soloud.pause();
        }
    }
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

float Player::getApproximateVolume(unsigned int channel)
{
    return soloud.getApproximateVolume(channel);
}

unsigned int Player::getActiveVoiceCount_internal()
{
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    unsigned int count = 0;
    for (auto &s : sounds)
    {
        count += s->handle.size();
    }
    return count;
}

PlayerErrors Player::play(
    unsigned int soundHash,
    unsigned int &handle,
    unsigned int busId,
    float volume,
    float pan,
    bool paused,
    bool looping,
    double loopingStartAt)
{
    ActiveSound *sound = findByHash(soundHash);

    if (sound == nullptr)
        return soundHashNotFound;

    // A BufferStream using `release` buffer type can only have one instance.
    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM &&
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->getBufferingType() == BufferingType::RELEASED &&
        sound->handle.size() > 0)
    {
        return bufferStreamCanBePlayedOnlyOnce;
    }

    // Check if playing this sound will exceed the maximum number of voice counts. If true, then
    // check if [soudHash] has other instances playing. If true remove the first and play the new one.
    // If no other instances are playing, this sound cannot be played and return an error.
    // Issue https://github.com/alnitak/flutter_soloud/issues/204
    if (getActiveVoiceCount_internal() >= getMaxActiveVoiceCount())
    {
        if (sound->handle.size() > 0)
        {
            stop(sound->handle[0].handle);
        }
        else
        {
            return PlayerErrors::maxActiveVoiceCountReached;
        }
    }

    // Ensure miniaudio device is started if it's stopped, ie by an interruption.
    soloud.resume();

    handle = 0;
    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.play(*sound->sound.get(), volume, pan, paused, 0);
    }
    else
    {
        auto it = busMap.find(busId);
        if (it != busMap.end())
            newHandle = it->second.bus.play(*sound->sound.get(), volume, pan, paused);
        else
            return PlayerErrors::busIdNotFound;
    }

    if (newHandle != 0)
    {
        sound->handle.push_back({newHandle, MAX_DOUBLE, false});
        // Check if this buffer has enough data to be played
        if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
        {
            static_cast<SoLoud::BufferStream *>(sound->sound.get())->checkBuffering(0);
        }
    }

    if (looping)
    {
        setLoopPoint(newHandle, loopingStartAt);
        setLooping(newHandle, true);
    }
    handle = newHandle;
    return PlayerErrors::noError;
}

void Player::stop(unsigned int handle)
{
    soloud.stop(handle);
    // After stopping, check if there are any remaining active voices.
    // If no voices are active, pause the audio device to allow the OS
    // to properly manage the audio session.
    pauseEngine();
}

void Player::removeHandle(unsigned int handle)
{
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    if (sounds.empty())
    {
        return;
    }

    bool found = false;
    size_t i = 0;
    while (i < sounds.size() && !found)
    {
        auto const &sound = sounds[i];
        if (sound)
        { // Check if unique_ptr is valid
            size_t n = 0;
            while (n < sound->handle.size() && !found)
            {
                if (sound->handle[n].handle == handle)
                {
                    sound->handle.erase(sound->handle.begin() + n);
                    found = true;
                }
                ++n;
            }
        }
        ++i;
    }
}

void Player::disposeSound(unsigned int soundHash)
{
    std::unique_ptr<ActiveSound> soundToDestroy;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        if (sounds.empty())
        {
            return;
        }

        auto it = std::find_if(sounds.begin(), sounds.end(),
                               [soundHash](const std::unique_ptr<ActiveSound> &sound)
                               {
                                   return sound->soundHash == soundHash;
                               });

        if (it != sounds.end())
        {
            // Stop all handles for this sound before destroying to prevent
            // the audio thread from accessing filters during destruction.
            for (auto &handleInfo : it->get()->handle)
            {
                soloud.stop(handleInfo.handle);
            }

            // Mark BufferStream for destruction before removing it
            if (it->get()->soundType == SoundType::TYPE_BUFFER_STREAM)
            {
                auto *bufferStream = static_cast<SoLoud::BufferStream *>(it->get()->sound.get());
                if (bufferStream != nullptr)
                {
                    bufferStream->markForDestruction();
                }
            }

            // Clear all filters from this sound BEFORE moving it out.
            // This prevents the audio thread from accessing filter instances
            // when the sound is destroyed.
            if (it->get()->sound)
            {
                for (int i = 0; i < FILTERS_PER_STREAM; i++)
                {
                    it->get()->sound->setFilter(i, nullptr);
                }
            }

            // Move the sound out of the vector before erasing
            soundToDestroy = std::move(*it);
            sounds.erase(it);
        }
    }
    // Sound (and its filters) is destroyed here when soundToDestroy goes out of scope

    // After disposing a sound, check if there are any remaining active voices.
    // If no voices are active, pause the audio device.
    pauseEngine();
}

void Player::disposeAllSound()
{
    // Stop all voices first. This stops all active audio processing.
    soloud.stopAll();

    // Pause the audio device BEFORE destroying sounds to ensure the audio thread
    // is not accessing filter memory. This prevents race conditions where the
    // audio thread crashes trying to access freed filter instances.
    soloud.pause();

    std::vector<std::unique_ptr<ActiveSound>> soundsToDestroy;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);

        // First, remove all filters from sounds while the audio thread is paused.
        // This prevents the audio thread from accessing filter instances during destruction.
        for (auto &sound : sounds)
        {
            if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
            {
                auto *bufferStream = static_cast<SoLoud::BufferStream *>(sound->sound.get());
                if (bufferStream != nullptr)
                {
                    bufferStream->markForDestruction();
                }
            }
            // Clear all filters from this sound
            if (sound->sound)
            {
                for (int i = 0; i < FILTERS_PER_STREAM; i++)
                {
                    sound->sound->setFilter(i, nullptr);
                }
            }
        }

        // Clear global filters
        for (int i = 0; i < FILTERS_PER_STREAM; i++)
        {
            soloud.setGlobalFilter(i, nullptr);
        }

        // Move all sounds out to destroy them after releasing the lock
        soundsToDestroy = std::move(sounds);
        sounds.clear();
    }
    // Sounds (and their filters) are destroyed here when soundsToDestroy goes out of scope
}

void Player::clearDartCallbackRegistrations()
{
    setVoiceEndedCallback(nullptr);
    setStateChangedCallback(nullptr);

    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    for (auto &sound : sounds)
    {
        if (sound != nullptr &&
            sound->soundType == SoundType::TYPE_BUFFER_STREAM &&
            sound->sound != nullptr)
        {
            static_cast<SoLoud::BufferStream *>(sound->sound.get())
                ->clearDartCallbacks();
        }
    }
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

    // Ensure miniaudio device is started if it's stopped, ie by an interruption.
    soloud.resume();

    SoLoud::result result = speech.setText(textToSpeech.c_str());

    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    sounds.push_back(std::make_unique<ActiveSound>());
    sounds.back().get()->completeFileName = std::string("");
    if (result == SoLoud::SO_NO_ERROR)
    {
        handle = soloud.play(speech);
        sounds.back().get()->soundHash = handle;
        sounds.back().get()->soundType = TYPE_TEXT_TO_SPEECH;
        sounds.back().get()->filters = std::make_unique<Filters>(&soloud, sounds.back().get(), nullptr);
        sounds.back().get()->handle.push_back({handle, MAX_DOUBLE, false});
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

float fftData[256];
float *Player::calcFFT(bool *isTheSameAsBefore)
{
    float *currentWave = soloud.calcFFT();
    if (memcmp(fftData, currentWave, sizeof(fftData)) != 0)
    {
        *isTheSameAsBefore = false;
    }
    else
    {
        *isTheSameAsBefore = true;
    }
    memcpy(fftData, currentWave, sizeof(fftData));

    return fftData;
}

float waveData[256];
float *Player::getWave(bool *isTheSameAsBefore)
{
    float *currentWave = soloud.getWave();
    if (memcmp(waveData, currentWave, sizeof(waveData)) != 0)
    {
        *isTheSameAsBefore = false;
    }
    else
    {
        *isTheSameAsBefore = true;
    }
    memcpy(waveData, currentWave, sizeof(waveData));

    return waveData;
}

// The length in seconds
double Player::getLength(unsigned int soundHash)
{
    auto const &s = findByHash(soundHash);

    if (s == nullptr || s->soundType == TYPE_SYNTH || s->soundType == TYPE_TEXT_TO_SPEECH)
        return 0.0;
    if (s->soundType == TYPE_WAV)
        return static_cast<SoLoud::Wav *>(s->sound.get())->getLength();
    if (s->soundType == TYPE_BUFFER_STREAM)
        return static_cast<SoLoud::BufferStream *>(s->sound.get())->getLength();
    if (s->soundType == TYPE_WAVSTREAM)
        return static_cast<SoLoud::WavStream *>(s->sound.get())->getLength();
    return 0.0;
}

// time in seconds
PlayerErrors Player::seek(SoLoud::handle handle, float time)
{
    if (!mInited)
        return backendNotInited;

    ActiveSound *sound = findByHandle(handle);
    bool isGroupHandle = soloud.isVoiceGroup(handle);

    if ((sound == nullptr || sound->soundType == TYPE_SYNTH) && !isGroupHandle)
        return invalidParameter;

    // A BufferStream using `release` buffer type cannot use seek.
    if (sound != nullptr && sound->soundType == SoundType::TYPE_BUFFER_STREAM &&
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->getBufferingType() == BufferingType::RELEASED)
    {
        return bufferStreamWithReleasedBufferTypeCannotBeSeeked;
    }

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
    pan = std::clamp(pan, -1.0f, 1.0f);
    soloud.setPan(handle, pan);
}

void Player::setPanAbsolute(SoLoud::handle handle, float panLeft, float panRight)
{
    panLeft = std::clamp(panLeft, -1.0f, 1.0f);
    panRight = std::clamp(panRight, -1.0f, 1.0f);
    soloud.setPanAbsolute(handle, panLeft, panRight);
}

bool Player::isValidHandle(SoLoud::handle handle)
{
    return soloud.isValidVoiceHandle(handle) || soloud.isVoiceGroup(handle);
}

int Player::countAudioSource(unsigned int soundHash)
{
    auto const &s = findByHash(soundHash);

    if (s == nullptr)
        return 0;

    SoLoud::AudioSource *as;
    switch (s->soundType)
    {
    case TYPE_SYNTH:
        return 0;
    case TYPE_WAV:
        as = static_cast<SoLoud::Wav *>(s->sound.get());
    case TYPE_WAVSTREAM:
        as = static_cast<SoLoud::WavStream *>(s->sound.get());
    case TYPE_BUFFER_STREAM:
        as = static_cast<SoLoud::BufferStream *>(s->sound.get());
    default:
        return 0;
    }
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

void Player::setInaudibleBehavior(SoLoud::handle handle, bool mustTick, bool kill)
{
    soloud.setInaudibleBehavior(handle, mustTick, kill);
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
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    int i = 0;
    while (i < (int)sounds.size())
    {
        int index = 0;
        while (sounds[i].get() && index < (int)sounds[i].get()->handle.size())
        {
            if (sounds[i].get()->handle[index].handle == handle)
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
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    auto const &s = std::find_if(sounds.begin(), sounds.end(),
                                 [&](std::unique_ptr<ActiveSound> const &f)
                                 { return f->soundHash == soundHash; });
    if (s == sounds.end())
        return nullptr;

    return s->get();
}

/////////////////////////////////////////
/// voice groups
/////////////////////////////////////////

unsigned int Player::createVoiceGroup()
{
    unsigned int ret = soloud.createVoiceGroup();
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

PlayerErrors Player::play3d(
    unsigned int soundHash,
    unsigned int &handle,
    float posX,
    float posY,
    float posZ,
    float velX,
    float velY,
    float velZ,
    float volume,
    bool paused,
    unsigned int busId,
    bool looping,
    double loopingStartAt)
{
    ActiveSound *sound = findByHash(soundHash);
    if (sound == 0)
        return soundHashNotFound;

    // A BufferStream using `release` buffer type can only have one instance.
    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM &&
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->getBufferingType() == BufferingType::RELEASED &&
        sound->handle.size() > 0)
    {
        return bufferStreamCanBePlayedOnlyOnce;
    }

    // Check if by playing this sound will exceed the maximum number of voice count. If true, then
    // check if [soudHash] has other instances playing. If true remove the first and play the new one.
    // If there are no other instances playing, this sound cannot be played and return an error.
    // Issue https://github.com/alnitak/flutter_soloud/issues/204
    if (getActiveVoiceCount_internal() >= getMaxActiveVoiceCount())
    {
        if (sound->handle.size() > 0)
        {
            stop(sound->handle[0].handle);
        }
        else
        {
            return PlayerErrors::maxActiveVoiceCountReached;
        }
    }

    // Ensure miniaudio device is started if it's stopped, ie by an interruption.
    soloud.resume();

    handle = 0;
    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.play3d(
            *sound->sound.get(),
            posX, posY, posZ,
            velX, velY, velZ,
            volume,
            paused,
            0);
    }
    else
    {
        auto it = busMap.find(busId);
        if (it != busMap.end())
            newHandle = it->second.bus.play3d(
                *sound->sound.get(),
                posX, posY, posZ,
                velX, velY, velZ,
                volume,
                paused);
        else
            return PlayerErrors::busIdNotFound;
    }

    if (newHandle != 0)
    {
        sound->handle.push_back({newHandle, MAX_DOUBLE, false});
        // Check if this buffer has enough data to be played
        if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
        {
            static_cast<SoLoud::BufferStream *>(sound->sound.get())->checkBuffering(0);
        }
    }
    if (looping)
    {
        seek(newHandle, loopingStartAt);
        setLoopPoint(newHandle, loopingStartAt);
        setLooping(newHandle, true);
        setPause(newHandle, paused);
    }
    handle = newHandle;
    return PlayerErrors::noError;
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

/////////////////////////////////////////
/// Mixing Bus
/////////////////////////////////////////

unsigned int Player::createBus()
{
    unsigned int id = ++busIdCounter;
    busMap.try_emplace(id, id, &soloud);
    return id;
}

void Player::destroyBus(unsigned int busId)
{
    busMap.erase(busId);
}

unsigned int Player::busPlayOnEngine(unsigned int busId, float volume,
                                     bool paused)
{
    if (!mInited)
        return 0;
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return 0;
    SoLoud::handle handle = soloud.play(it->second.bus, volume, 0.0f, paused);
    it->second.handle = handle;
    // Playing a sound inside a bus decreases the volume compared to playing it directly.
    // https://github.com/jarikomppa/soloud/issues/395#issuecomment-4148675275
    soloud.setPanAbsolute(handle, 1.0f, 1.0f);
    return handle;
}

int Player::busSetChannels(unsigned int busId, unsigned int channels)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return -1; // bus not found
    return static_cast<int>(it->second.bus.setChannels(channels));
}

void Player::busSetVisualizationEnable(unsigned int busId, bool enable)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return;
    it->second.bus.setVisualizationEnable(enable);
}

float *Player::busCalcFFT(unsigned int busId)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return nullptr;
    return it->second.bus.calcFFT();
}

float *Player::busGetWave(unsigned int busId)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return nullptr;
    return it->second.bus.getWave();
}

float Player::busGetApproximateVolume(unsigned int busId,
                                      unsigned int channel)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return 0.0f;
    return it->second.bus.getApproximateVolume(channel);
}

void Player::busAnnexSound(unsigned int busId, unsigned int voiceHandle)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return;
    it->second.bus.annexSound(voiceHandle);
}

unsigned int Player::busGetActiveVoiceCount(unsigned int busId)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return 0;
    unsigned int ret = it->second.bus.getActiveVoiceCount();
    return ret;
}

BusData *Player::findBusData(unsigned int busId)
{
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return nullptr;
    return &it->second;
}

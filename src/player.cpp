#include "soloud_common.h"
#include "player.h"
#include "device_lifecycle_test_hooks.h"
#include "audiobuffer/circular_float_buffer.h"
#include "audiobuffer/pull_buffer_stream.h"
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
    constexpr int64_t kDefaultIdleTimeoutMs = 500;
    constexpr int64_t kMaxIdleWaitChunkMs = 24LL * 60 * 60 * 1000;

    // Device lifecycle configuration outlives each Player instance so a native
    // deinit/reinit cycle preserves the user's timeout policy.
    std::atomic<int64_t> gAudioDeviceIdleTimeoutMs{kDefaultIdleTimeoutMs};

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

/// Translate a `SoLoud::SOLOUD_ERRORS` value into a `PlayerErrors` one.
///
/// The two enums are NOT interchangeable: they agree up to
/// `FILE_LOAD_FAILED` (3) and then diverge, because `PlayerErrors` inserts
/// `fileAlreadyLoaded` at 4. Casting one to the other silently turns, for
/// example, SoLoud's `NOT_IMPLEMENTED` (6) into `outOfMemory` (6).
static PlayerErrors fromSoLoudError(SoLoud::result result)
{
    switch (result)
    {
    case SoLoud::SO_NO_ERROR:
        return noError;
    case SoLoud::INVALID_PARAMETER:
        return invalidParameter;
    case SoLoud::FILE_NOT_FOUND:
        return fileNotFound;
    case SoLoud::FILE_LOAD_FAILED:
        return fileLoadFailed;
    case SoLoud::DLL_NOT_FOUND:
        return dllNotFound;
    case SoLoud::OUT_OF_MEMORY:
        return outOfMemory;
    case SoLoud::NOT_IMPLEMENTED:
        return notImplemented;
    default:
        return unknownError;
    }
}

Player::Player() : mFilters(&soloud, nullptr, nullptr),
                   mPauseThreadRunning(false),
                   mIdleTimeoutMs(
                       gAudioDeviceIdleTimeoutMs.load(
                           std::memory_order_acquire))
{
}

Player::~Player()
{
    mLifecycleRequestsAccepted.store(false, std::memory_order_release);
    soloud.setAudioInterruptionCallback(nullptr, nullptr);

    // If the scheduler was started, stop it before touching Soloud.
    stopPauseEngineScheduler();

    if (!mInited.load(std::memory_order_acquire))
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
    if (!mInited.load(std::memory_order_acquire))
        return;

    SOLOUD_TEST_BARRIER(playerDisposeEntered);

    // Reject new lifecycle work before waking and joining the scheduler.
    mLifecycleRequestsAccepted.store(false, std::memory_order_release);
    soloud.setAudioInterruptionCallback(nullptr, nullptr);
    mInterruptionActive.store(false, std::memory_order_release);
    mInited.store(false, std::memory_order_release);
    stopPauseEngineScheduler();

    // The scheduler join above waits for any device operation it was already
    // performing. Keep the same serialization lock held through the remaining
    // stop and backend teardown so no real device operation can overlap it.
    std::lock_guard<std::mutex> operationLock(
        mDeviceLifecycleOperationMutex);

    // Unregister callbacks before stopping voices. In particular, keep stale
    // Dart callback pointers from being used while teardown destroys sources.
    clearDartCallbackRegistrations();
    setVoiceEndedCallback(nullptr);
    setStateChangedCallback(nullptr);
    setVoiceInactiveCallback(nullptr);

    // Player::dispose() is the sole owner of native sound destruction during
    // engine teardown. This helper deliberately does not evaluate idle policy
    // or request a restart when the timeout is indefinite.
    stopDeviceAndDestroyAllSounds();
    soloud.deinit();
}

void Player::setVoiceEndedCallback(void (*voiceEndedCallback)(unsigned int *))
{
    soloud.setVoiceEndedCallback(voiceEndedCallback);
}

void Player::setVoiceInactiveCallback(void (*voiceInactiveCallback)())
{
    soloud.setVoiceInactiveCallback(voiceInactiveCallback);
}

void Player::setStateChangedCallback(void (*stateChangedCallback)(unsigned int))
{
    soloud.setStateChangedCallback(stateChangedCallback);
}

// Defined in the miniaudio backend (soloud_miniaudio.cpp). Forward-declared
// here so we don't need to pull in the backend-internal header.
namespace SoLoud { void miniaudio_setLowLatency(bool aLowLatency); }
namespace SoLoud { SoLoud::result miniaudio_stopAudioDevice(); }
namespace SoLoud { SoLoud::result miniaudio_startAudioDevice(); }
namespace SoLoud { unsigned int miniaudio_getAudioDeviceState(); }

PlayerErrors Player::init(unsigned int sampleRate, unsigned int bufferSize, unsigned int channels, int deviceID, bool lowLatency, unsigned int devicePeriodFrames, unsigned int renderAheadFrames)
{
    if (mInited.load(std::memory_order_acquire))
        return playerAlreadyInited;

    mLifecycleRequestsAccepted.store(false, std::memory_order_release);

    // Refresh from the published policy: the constructor ran earlier, and
    // setAudioDeviceIdleTimeout() may have been called in between.
    mIdleTimeoutMs.store(
        gAudioDeviceIdleTimeoutMs.load(std::memory_order_acquire),
        std::memory_order_release);

    // Choose the device performance profile before SoLoud opens the backend.
    SoLoud::miniaudio_setLowLatency(lowLatency);

    // Anything below -1 is never a valid device selector.
    if (deviceID < -1)
        return invalidParameter;

    // -1 leaves this null, which asks miniaudio for the OS default device.
    void *playbackInfos_id = nullptr;
    // Must outlive `soloud.init()`: `playbackInfos_id` points into it.
    std::vector<PlaybackDevice> devices;
    if (deviceID >= 0)
    {
        // Get the device list and find the requested device
        devices = listPlaybackDevices();
        size_t const index = (size_t)deviceID;
        if (index >= devices.size())
            return noPlaybackDevicesFound;
        // Use the stored device ID from the PlaybackDevice struct
        playbackInfos_id = (void *)&devices[index].deviceId;
    }

    // Configure the render-ahead ring before the backend opens the device:
    // miniaudio_init() reads this to pick the device period, and
    // postinit_internal() allocates the ring.
    soloud.setRenderAheadConfig(devicePeriodFrames, renderAheadFrames);

    // initialize SoLoud.
    SoLoud::result result;
    try
    {
        result = soloud.init(0, SoLoud::Soloud::MINIAUDIO, sampleRate, bufferSize, channels, playbackInfos_id);
    }
    catch (...)
    {
        // SoLoud may already have allocated its audio mutex or partially
        // opened the backend. Roll all of that back before reporting failure.
        soloud.deinit();
        return backendNotInited;
    }

    if (result != SoLoud::SO_NO_ERROR)
    {
        // A failed backend init can still leave partial miniaudio/SoLoud
        // resources allocated. Initialization failure must be all-or-nothing.
        soloud.deinit();
        return backendNotInited;
    }
    else
    {
        // soloud.init() has already opened and started the device, so the
        // mixer is reading mPostClipScaler by the time this runs. The audio
        // mutex does NOT order the two -- clip_internal() runs after
        // unlockAudioMutex_internal() -- so the field is atomic instead; see
        // its declaration in soloud.h.
        soloud.setPostClipScaler(1.0f);
        mSampleRate = sampleRate;
        mBufferSize = bufferSize;
        mChannels = channels;
        // Start the deferred-pause scheduler now that the engine is in use.
        // Thread creation is part of initialization: if it fails, leave no
        // initialized backend or partially published lifecycle state behind.
        try {
            startPauseEngineScheduler();
        } catch (...) {
            stopPauseEngineScheduler();
            soloud.deinit();
            return backendNotInited;
        }
        mLifecycleRequestsAccepted.store(true, std::memory_order_release);
        mInterruptionActive.store(false, std::memory_order_release);
        soloud.setAudioInterruptionCallback(
            &Player::audioInterruptionCallback, this);
        // Publish initialized state only after all lifecycle support,
        // including interruption routing, is ready.
        mInited.store(true, std::memory_order_release);
        // Treat the freshly initialized engine as having just entered the idle
        // state and apply the configured idle timeout. With a finite timeout
        // (including zero) and nothing playing yet, this schedules the deferred
        // device stop; an indefinite (negative) timeout keeps the device — just
        // started by soloud.init() — running. Any play/unpause before the
        // deadline cancels the pending stop.
        if (mIdleTimeoutMs.load() >= 0)
            pauseEngine();
    }
    // `result` is `SO_NO_ERROR` here (anything else returned above), but map it
    // rather than cast: the two error enums are not interchangeable.
    return fromSoLoudError(result);
}

PlayerErrors Player::changeDevice(int deviceID)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return backendNotInited;

    // Anything below -1 is never a valid device selector.
    if (deviceID < -1)
        return invalidParameter;

    // `nullptr` asks miniaudio for the current OS default output device. The
    // default device doesn't depend on a successful enumeration, so only
    // explicit IDs need the device list (same as `init()` does). Resolve it
    // before entering the lifecycle operation below: enumeration is slow and
    // must not run while holding mDeviceLifecycleOperationMutex.
    void *playbackInfos_id = nullptr;
    // Must outlive `miniaudio_changeDevice()`: `playbackInfos_id` points into it.
    std::vector<PlaybackDevice> devices;
    if (deviceID >= 0)
    {
        // Get the device list and find the requested device
        devices = listPlaybackDevices();
        size_t const index = (size_t)deviceID;
        if (index >= devices.size())
            return noPlaybackDevicesFound;
        // Use the stored device ID from the PlaybackDevice struct
        playbackInfos_id = (void *)&devices[index].deviceId;
    }

    bool shouldStartReplacement = false;
    PlayerErrors changeResult = noError;
    {
        std::lock_guard<std::mutex> operationLock(
            mDeviceLifecycleOperationMutex);
        // Re-check under the lock: the engine can be torn down while the
        // enumeration above is running.
        if (!mInited.load(std::memory_order_acquire) ||
            !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
            return backendNotInited;

        // Taken before the state this decision reads, so playback starting
        // mid-swap is detectable as newer intent below.
        const uint64_t token = currentDeviceRequestGeneration();

        // The replacement device only has to be started if the old one was
        // running, or if the idle policy says it should be. A device stopped by
        // stopAudioDevice() or by the idle timeout stays stopped across the
        // swap.
        const AudioDeviceState previousState = getAudioDeviceState();
        shouldStartReplacement =
            soloud.getActiveVoiceCount() != 0 ||
            mIdleTimeoutMs.load(std::memory_order_acquire) < 0 ||
            previousState == audioDeviceStarted ||
            previousState == audioDeviceStarting;

        SOLOUD_TEST_BARRIER(changeDeviceStartDecided);

        // Device replacement supersedes requests that targeted the old device,
        // but only those: a play() that created its voice after the decision
        // above queued a start that is newer than this swap, and erasing it
        // would leave the replacement stopped under active playback.
        if (!cancelSupersededDeviceRequests(token))
        {
            // Bring the replacement up here rather than relying solely on the
            // surviving request, so there is no window where a live voice is
            // rendering into a stopped device.
            shouldStartReplacement = true;
        }

        const SoLoud::result result =
            soloud.miniaudio_changeDevice(playbackInfos_id);

        // `NOT_IMPLEMENTED` means the engine was built without the miniaudio
        // backend, so there is no device to swap at all.
        if (result == SoLoud::NOT_IMPLEMENTED)
            changeResult = notImplemented;
        // Anything else non-zero is `UNKNOWN_ERROR`: the replacement device
        // could not be initialized or started. The engine itself may well still
        // be initialized, hence not `backendNotInited`.
        else if (result != SoLoud::SO_NO_ERROR)
            changeResult = audioDeviceFailedToStart;
        else if (shouldStartReplacement)
        {
            // Use the normal lifecycle start path so iOS reactivates the
            // AVAudioSession before starting the replacement Audio Unit.
            changeResult = performAudioDeviceStart();
        }
    }

    if (changeResult == noError && shouldStartReplacement)
        evaluateAudioDeviceIdle();
    return changeResult;
}

// List available playback devices.
std::vector<PlaybackDevice> Player::listPlaybackDevices()
{
    // printf("***************** LIST DEVICES START\n");
    ma_context context;
    ma_uint32 playbackCount;
    // Both info arrays belong to `context` and are freed by `ma_context_uninit`
    // below, so they must stay local: keeping them alive past this function
    // (as a member) would only leave a dangling pointer behind.
    ma_device_info *pPlaybackInfos;
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
        // `std::string` takes a copy: the source dies with the context, and a
        // `strdup()` here would leak since nothing ever freed these names.
        cd.name = pPlaybackInfos[i].name;
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
    return mInited.load(std::memory_order_acquire);
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
    case hashIsNotAPullBufferStream:
        return "error: hash is not a pull buffer stream!";
    case invalidPullBufferState:
        return "error: pull buffer stream is in an invalid state!";
    case audioDeviceFailedToStart:
        return "error: the output audio device could not be started!";
    case failedToStartPlayback:
        return "error: failed to start the playback, no valid voice handle!";
    }
    return "Other error";
}

PlayerErrors Player::loadFile(
    const std::string &completeFileName,
    bool loadIntoMem,
    unsigned int *hash)
{
    if (!mInited.load(std::memory_order_acquire))
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

    PlayerErrors loadError = fromSoLoudError(result);
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
    if (!mInited.load(std::memory_order_acquire))
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

    PlayerErrors loadError = fromSoLoudError(result);
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
    if (!mInited.load(std::memory_order_acquire))
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

PlayerErrors Player::setPullBufferStream(
    unsigned int &hash,
    unsigned int bufferSizeBytes,
    double bufferTriggerPosition,
    unsigned int sampleRate,
    unsigned int channels,
    BufferType format,
    uint64_t audioSizeBytes,
    dartOnBufferingCallback_t onBufferingCallback,
    dartOnMetadataCallback_t onMetadataCallback,
    dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
    dartOnAudioDurationCallback_t onAudioDurationCallback)
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

    newSound.get()->sound = std::make_unique<SoLoud::PullBufferStream>();
    newSound.get()->soundType = SoundType::TYPE_PULL_BUFFER_STREAM;

    auto *pullStream = static_cast<SoLoud::PullBufferStream *>(newSound.get()->sound.get());
    PlayerErrors e = pullStream->setPullBufferStream(
        this, newSound.get(), bufferSizeBytes, bufferTriggerPosition,
        sampleRate, channels, format, audioSizeBytes,
        onBufferingCallback, onMetadataCallback, onMoreDataIsNeededCallback,
        onAudioDurationCallback);

    newSound.get()->filters = std::make_unique<Filters>(&soloud, newSound.get(), nullptr);
    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        sounds.push_back(std::move(newSound));
    }

    return e;
}

PlayerErrors Player::resetPullBufferStream(unsigned int hash)
{
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_PULL_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    static_cast<SoLoud::PullBufferStream *>(s->sound.get())->resetPullBufferStream();
    return PlayerErrors::noError;
}

PlayerErrors Player::addPullBufferDataStream(
    unsigned int hash,
    const unsigned char *data,
    unsigned int aDataLen,
    uint64_t offset)
{
    auto const s = findByHash(hash);

    if (s == nullptr)
        return PlayerErrors::soundHashNotFound;

    if (s->soundType != SoundType::TYPE_PULL_BUFFER_STREAM)
        return PlayerErrors::hashIsNotAPullBufferStream;

    return static_cast<SoLoud::PullBufferStream *>(s->sound.get())
        ->addAudioData(data, aDataLen, offset);
}

PlayerErrors Player::getPullBufferTimeRange(
    unsigned int hash,
    double *startTime,
    double *endTime)
{
    auto const s = findByHash(hash);

    if (s == nullptr)
        return PlayerErrors::soundHashNotFound;

    if (s->soundType != SoundType::TYPE_PULL_BUFFER_STREAM)
        return PlayerErrors::hashIsNotAPullBufferStream;

    auto *pullStream = static_cast<SoLoud::PullBufferStream *>(s->sound.get());
    double start = 0.0;
    double end = 0.0;
    pullStream->getBufferTimeRange(start, end);
    if (startTime != nullptr) *startTime = start;
    if (endTime != nullptr) *endTime = end;
    return PlayerErrors::noError;
}

PlayerErrors Player::addAudioDataStream(
    unsigned int hash,
    const unsigned char *data,
    unsigned int aDataLen)
{
    // Hold sounds_mutex for the whole call: disposeSound()/disposeAllSound()
    // destroy the ActiveSound (and its Buffer's mutex) only after acquiring
    // it, so this guarantees the BufferStream outlives the addData() call.
    // Otherwise a feeder thread can lock a destroyed std::mutex, which
    // aborts on Android (HandleUsingDestroyedMutex).
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    auto const s = findByHash(hash);

    if (s == nullptr)
        return PlayerErrors::soundHashNotFound;

    if (s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return hashIsNotABufferStream;

    return static_cast<SoLoud::BufferStream *>(s->sound.get())->addData(data, aDataLen, false);
}

PlayerErrors Player::resetBufferStream(unsigned int hash)
{
    // See addAudioDataStream() for why the lock must span the whole call.
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
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
    // See addAudioDataStream() for why the lock must span the whole call.
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
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
    // See addAudioDataStream() for why the lock must span the whole call.
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
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
    // See addAudioDataStream() for why the lock must span the whole call.
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    static_cast<SoLoud::BufferStream *>(s->sound.get())->setDataIsEnded();
    return PlayerErrors::noError;
}

PlayerErrors Player::getBufferSize(unsigned int hash, unsigned int *sizeInBytes)
{
    // See addAudioDataStream() for why the lock must span the whole call.
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    auto const s = findByHash(hash);

    if (s == nullptr || s->soundType != SoundType::TYPE_BUFFER_STREAM)
        return PlayerErrors::soundHashNotFound;

    auto *bufferStream = static_cast<SoLoud::BufferStream *>(s->sound.get());
    std::lock_guard<std::recursive_mutex> bufferLock(bufferStream->mBuffer.bufferMutex);
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
    if (!mInited.load(std::memory_order_acquire))
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
        auto basicWave = std::make_unique<Basicwave>((SoLoud::Soloud::WAVEFORM)waveform, superWave, detune, scale);
        basicWave->setSamplerate(mSampleRate);
        sounds.back().get()->sound = std::move(basicWave);
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

void Player::applyPauseState(unsigned int handle, bool pause, bool isUserAction)
{
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

    if (!pause)
    {
        // Unpausing queues device startup on the lifecycle scheduler instead of
        // starting the device inline: ma_device_start() blocks for tens of
        // milliseconds (seconds on some Android devices) and this runs on the
        // FFI (UI) thread.
        resumeEngine();
        return;
    }

    // When pausing, let the idle policy decide whether the device should be
    // stopped. The scheduler performs the authoritative active-voice check.
    evaluateAudioDeviceIdle();
}

PlayerErrors Player::setPause(unsigned int handle, bool pause, bool isUserAction)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
    {
        return PlayerErrors::backendNotInited;
    }

    if (!isValidHandle(handle))
        return PlayerErrors::soundHandleNotFound;

    applyPauseState(handle, pause, isUserAction);
    return PlayerErrors::noError;
}

PlayerErrors Player::pauseSwitch(unsigned int handle)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
    {
        return PlayerErrors::backendNotInited;
    }

    if (!isValidHandle(handle))
        return PlayerErrors::soundHandleNotFound;

    applyPauseState(handle, !soloud.getPause(handle), true);

    return PlayerErrors::noError;
}

void Player::evaluateAudioDeviceIdle()
{
    std::lock_guard<std::mutex> interruptionLock(mInterruptionMutex);
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire) ||
        mInterruptionActive.load(std::memory_order_acquire))
    {
        return;
    }
#ifdef __EMSCRIPTEN__
    // The mixer invokes the inactive callback only after releasing SoLoud's
    // audio mutex, so this count is safe even for scheduled/natural endings.
    if (soloud.getActiveVoiceCount() == 0 &&
        mIdleTimeoutMs.load(std::memory_order_acquire) >= 0)
        soloud.pause();
#else
    // Do not inspect SoLoud voice state on the FFI caller thread.
    //
    // The lifecycle scheduler performs the authoritative active-voice check
    // after the configured timeout and immediately before stopping the
    // device.
    requestDeviceLifecycle(DeviceLifecycleRequest::idleStop);
#endif
}

void Player::audioInterruptionCallback(void *context, bool began)
{
    if (context != nullptr)
        static_cast<Player *>(context)->handleAudioInterruption(began);
}

void Player::handleAudioInterruption(bool began)
{
    std::lock_guard<std::mutex> interruptionLock(mInterruptionMutex);
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return;

    mInterruptionActive.store(began, std::memory_order_release);
#ifdef __EMSCRIPTEN__
    if (began)
    {
        soloud.pause();
    }
    else if (soloud.getActiveVoiceCount() != 0 ||
             mIdleTimeoutMs.load(std::memory_order_acquire) < 0)
    {
        // Web has no lifecycle scheduler thread. Its AudioContext operations
        // are nonblocking, so recover inline when policy requires it.
        soloud.resume();
    }
#else
    if (began)
    {
        // This immediate stop supersedes pending starts/idle deadlines. The
        // callback only posts work; the backend call runs on the scheduler.
        requestDeviceLifecycle(DeviceLifecycleRequest::interruptionStop);
    }
    else if (soloud.getActiveVoiceCount() != 0 ||
             mIdleTimeoutMs.load(std::memory_order_acquire) < 0)
    {
        // Resume only for active playback or indefinite keep-alive. A finite
        // timeout with an idle engine leaves the interruption-stopped device
        // stopped; later play/unpause starts it normally.
        requestDeviceLifecycle(DeviceLifecycleRequest::start);
    }
#endif
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
// persistent scheduler thread handles all device lifecycle requests.
void Player::pauseEngine()
{
    std::lock_guard<std::mutex> interruptionLock(mInterruptionMutex);
    if (!mLifecycleRequestsAccepted.load(std::memory_order_acquire) ||
        mInterruptionActive.load(std::memory_order_acquire))
        return;
#ifdef __EMSCRIPTEN__
    // Web: the wasm build is single-threaded (no pthreads), so the deferred
    // scheduler thread cannot run. Pause the device immediately instead. The
    // browser's AudioContext does not have the OS audio-session settling issue
    // that motivates the delay on native platforms.
    if (mInited.load(std::memory_order_acquire) &&
        soloud.getActiveVoiceCount() == 0 && mIdleTimeoutMs.load() >= 0)
        soloud.pause();
#else
    requestDeviceLifecycle(DeviceLifecycleRequest::idleStop);
#endif
}

// Restart the audio device off the UI thread. The native ma_device_start()
// blocks for tens of milliseconds (seconds on some Android devices) while the
// OS restarts the device, so running it inline on the FFI (UI) thread freezes
// the app. Post the request to the same scheduler thread that handles the
// deferred pause; it calls soloud.resume() there instead. A newer start request
// invalidates a pending deferred pause so a play() arriving during the pause
// coalescing window keeps the device running.
void Player::resumeEngine()
{
    std::lock_guard<std::mutex> interruptionLock(mInterruptionMutex);
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire) ||
        mInterruptionActive.load(std::memory_order_acquire))
        return;
#ifdef __EMSCRIPTEN__
    // Web: the wasm build is single-threaded (no pthreads), so there is no
    // scheduler thread. The AudioContext resume is effectively instant, so
    // start the device inline.
    soloud.resume();
#else
    requestDeviceLifecycle(DeviceLifecycleRequest::start);
#endif
}

void Player::publishAudioDeviceIdleTimeout(int64_t timeoutMs)
{
    gAudioDeviceIdleTimeoutMs.store(timeoutMs, std::memory_order_release);
}

void Player::applyPublishedAudioDeviceIdleTimeout()
{
    setAudioDeviceIdleTimeout(
        gAudioDeviceIdleTimeoutMs.load(std::memory_order_acquire));
}

void Player::setAudioDeviceIdleTimeout(int64_t timeoutMs)
{
    publishAudioDeviceIdleTimeout(timeoutMs);
    // Stored even when the engine is not initialized, so init() cannot start
    // from a value that was superseded before it ran.
    mIdleTimeoutMs.store(timeoutMs, std::memory_order_release);

    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return;

    if (timeoutMs < 0)
    {
        // Indefinite timeout: start the device now (off the UI thread) and
        // cancel any pending deferred idle-pause, so the device keeps running
        // even with no active voices.
        resumeEngine();
    }
    else
    {
        // Queue an idle-stop request immediately. The scheduler applies the
        // timeout and performs the authoritative active-voice check.
        evaluateAudioDeviceIdle();
    }
}

PlayerErrors Player::performAudioDeviceStop(bool explicitRequest)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return backendNotInited;

    // Web deliberately suppresses automatic idle pauses, but an explicit stop
    // must still operate the device. Native explicit and automatic stops both
    // reach the same miniaudio backend operation through these two entry paths.
    SoLoud::result result = explicitRequest
        ? SoLoud::miniaudio_stopAudioDevice()
        : soloud.pause();
    if (result != SoLoud::SO_NO_ERROR)
        return unknownError;
    return noError;
}

PlayerErrors Player::performAudioDeviceStart()
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return backendNotInited;
    if (mInterruptionActive.load(std::memory_order_acquire))
        return noError;

    SOLOUD_TEST_BARRIER(performAudioDeviceStartEntered);

    // Use the normal resume hook so iOS reactivates AVAudioSession before the
    // Audio Unit is restarted.
    SoLoud::result result = soloud.resume();
    if (result == SoLoud::SO_NO_ERROR)
        return noError;

    // The start failed. Rebuild the device and try once more before giving up.
    //
    // The backend keeps the stream open across an idle stop, so a device that
    // has been stopped for a while can be holding a stream the OS has since
    // invalidated. On Android this is the common case: AAudio only reports a
    // disconnect through the error callback of a *running* stream, so a stream
    // that is torn down while stopped -- a route change, or the framework
    // reclaiming resources from a backgrounded app -- is never rerouted, and
    // the staleness only surfaces here, as AAudioStream_requestStart failing.
    //
    // Replacing the device recreates the stream against the current default
    // output. Voices, loaded sources and filters all live in SoLoud rather than
    // in the device, so playback resumes where it left off.
    //
    // Callers hold mDeviceLifecycleOperationMutex, so this cannot interleave
    // with another device operation. One retry only: if a freshly built device
    // will not start either, the failure is not staleness.
    const SoLoud::result rebuilt = soloud.miniaudio_changeDevice(nullptr);
    if (rebuilt != SoLoud::SO_NO_ERROR)
        return audioDeviceFailedToStart;

    result = soloud.resume();
    if (result != SoLoud::SO_NO_ERROR)
        return audioDeviceFailedToStart;
    return noError;
}

void Player::reportAutomaticDeviceStartFailure()
{
    soloud.notifyStateChanged(
        (unsigned int)PlayerStateEvents::event_audio_device_start_failed);
}

void Player::invalidatePendingDeviceRequest()
{
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);
        mPendingDeviceRequest = DeviceLifecycleRequest::none;
        mIdleStopRequestedAfterImmediateOperation = false;
        mStartRequestedAfterInterruptionStop = false;
        ++mDeviceRequestGeneration;
    }
    mPauseCv.notify_one();
#endif
}

uint64_t Player::currentDeviceRequestGeneration()
{
#ifdef __EMSCRIPTEN__
    return 0;
#else
    std::lock_guard<std::mutex> lock(mPauseMutex);
    return mDeviceRequestGeneration;
#endif
}

bool Player::cancelSupersededDeviceRequests(uint64_t token)
{
#ifdef __EMSCRIPTEN__
    // Web posts no lifecycle requests at all: there is no scheduler thread, so
    // every path acts inline and there is nothing queued to lose.
    (void)token;
    return true;
#else
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);

        if (mDeviceRequestGeneration != token)
        {
            // Something landed after the caller's decision. Only immediate work
            // carries intent a direct operation must not erase; a newer idle
            // request is still safe to drop.
            const bool newerImmediateIntent =
                mPendingDeviceRequest == DeviceLifecycleRequest::start ||
                mPendingDeviceRequest ==
                    DeviceLifecycleRequest::interruptionStop ||
                mImmediateDeviceRequestInFlight ==
                    DeviceLifecycleRequest::start ||
                mImmediateDeviceRequestInFlight ==
                    DeviceLifecycleRequest::interruptionStop ||
                mStartRequestedAfterInterruptionStop;
            if (newerImmediateIntent)
                return false;
        }

        mPendingDeviceRequest = DeviceLifecycleRequest::none;
        mIdleStopRequestedAfterImmediateOperation = false;
        mStartRequestedAfterInterruptionStop = false;
        ++mDeviceRequestGeneration;
    }
    mPauseCv.notify_one();
    return true;
#endif
}

bool Player::isDeviceRequestCurrent(uint64_t generation)
{
#ifdef __EMSCRIPTEN__
    (void)generation;
    return true;
#else
    std::lock_guard<std::mutex> lock(mPauseMutex);
    return !mStopPauseThread &&
           mDeviceRequestGeneration == generation;
#endif
}

PlayerErrors Player::stopAudioDevice(bool force)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return backendNotInited;

    std::lock_guard<std::mutex> operationLock(
        mDeviceLifecycleOperationMutex);

    // Taken before the active-voice count is read, so a play() that creates its
    // voice and queues a start while this operation is deciding is detectable
    // as newer intent below. play() deliberately does not wait for this mutex,
    // so that window is reachable in ordinary use.
    const uint64_t token = currentDeviceRequestGeneration();

    // The conditional form is intentionally a successful no-op while any
    // voice is active.
    if (!force && soloud.getActiveVoiceCount() != 0)
        return noError;

    SOLOUD_TEST_BARRIER(stopAudioDeviceVoiceCountObserved);

    if (!cancelSupersededDeviceRequests(token))
    {
        // A start (playback, an unpause) or an OS interruption stop landed
        // after the decision above. It is newer than this stop, so it stays
        // queued.
        if (!force)
        {
            // The conditional form asked to stop only while idle, and the
            // engine is no longer idle. Leaving the device running is the
            // documented successful no-op.
            return noError;
        }
        // A forced stop is an explicit instruction and still stops the device.
        // The newer request survives, so the scheduler restarts the device
        // afterwards -- native acquisition order decides, and neither request
        // is lost.
    }

    return performAudioDeviceStop(true);
}

PlayerErrors Player::startAudioDevice()
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
        return backendNotInited;

    PlayerErrors result;
    {
        std::lock_guard<std::mutex> operationLock(
            mDeviceLifecycleOperationMutex);

        // An explicit start is an authoritative request from the app, so it
        // clears the interruption latch rather than being suppressed by it.
        //
        // Returning noError here without touching the device made this API
        // able to report success while leaving the device stopped, and the
        // latch can be stuck: iOS does not reliably deliver
        // AVAudioSessionInterruptionTypeEnded -- notably when the interruption
        // ends while the app is backgrounded or suspended -- and the flag is
        // otherwise only cleared by init()/deinit(). Once missed, every later
        // start was a silent no-op for the lifetime of the engine.
        //
        // If an interruption really is still in force the OS refuses to
        // activate the session and the start below fails, which surfaces a real
        // error instead of a false success. A genuine new interruption arriving
        // afterwards sets the flag again through the normal callback.
        //
        // Clearing the latch and cancelling superseded work happen under
        // mInterruptionMutex so they are atomic with respect to the OS
        // interruption callback. Without that, a genuine interruption arriving
        // in this window is indistinguishable from the stale flag this is meant
        // to clear, and the cancellation below would erase the stop it queued.
        uint64_t token;
        {
            std::lock_guard<std::mutex> interruptionLock(mInterruptionMutex);
            mInterruptionActive.store(false, std::memory_order_release);
            // Cancel a stale delayed idle stop before prewarming the device.
            // Nothing concurrent can be newer than this point: the latch and
            // the request queue moved together.
            invalidatePendingDeviceRequest();
            token = currentDeviceRequestGeneration();
        }

        SOLOUD_TEST_BARRIER(startAudioDeviceLatchCleared);

        result = performAudioDeviceStart();
        if (result == noError)
        {
            if (mInterruptionActive.load(std::memory_order_acquire))
            {
                // A genuine interruption arrived while the start was in flight,
                // so performAudioDeviceStart() skipped it. Reporting success
                // here is exactly the "succeeded but left the device stopped"
                // bug this API exists to avoid, and cancelling now would erase
                // the interruption stop the callback queued.
                result = audioDeviceFailedToStart;
            }
            else
            {
                // Only cancel work that predates the completed start.
                cancelSupersededDeviceRequests(token);
            }
        }
    }

    if (result == noError)
        evaluateAudioDeviceIdle();
    return result;
}

AudioDeviceState Player::getAudioDeviceState()
{
    if (!mInited.load(std::memory_order_acquire))
        return audioDeviceUninitialized;

    return (AudioDeviceState)SoLoud::miniaudio_getAudioDeviceState();
}

bool Player::requestDeviceLifecycle(DeviceLifecycleRequest request)
{
#ifdef __EMSCRIPTEN__
    (void)request;
    return false;
#else
    bool shouldNotify = false;

    {
        std::lock_guard<std::mutex> lock(mPauseMutex);

        if (!mPauseThreadRunning || mStopPauseThread)
            return false;

        const bool immediatePending =
            mPendingDeviceRequest == DeviceLifecycleRequest::start ||
            mPendingDeviceRequest ==
                DeviceLifecycleRequest::interruptionStop;

        const bool immediateInFlight =
            mImmediateDeviceRequestInFlight == DeviceLifecycleRequest::start ||
            mImmediateDeviceRequestInFlight ==
                DeviceLifecycleRequest::interruptionStop;

        switch (request)
        {
        case DeviceLifecycleRequest::idleStop:
            if (immediatePending || immediateInFlight)
            {
                // Do not replace the immediate operation or advance the
                // generation that validates it. Run a fresh idle timeout
                // after the immediate operation completes.
                mIdleStopRequestedAfterImmediateOperation = true;
                return true;
            }

            // A newer idle request restarts an existing idle deadline.
            mPendingDeviceRequest = DeviceLifecycleRequest::idleStop;
            ++mDeviceRequestGeneration;
            shouldNotify = true;
            break;

        case DeviceLifecycleRequest::start:
            // An OS interruption stop has higher priority than startup.
            if (mPendingDeviceRequest ==
                    DeviceLifecycleRequest::interruptionStop ||
                mImmediateDeviceRequestInFlight ==
                    DeviceLifecycleRequest::interruptionStop)
            {
                // The interruption stop retains priority, but the recovery
                // start must run after it completes if the interruption has
                // ended.
                mIdleStopRequestedAfterImmediateOperation = false;
                mStartRequestedAfterInterruptionStop = true;
                return true;
            }

            // Playback startup supersedes all older idle work.
            mStartRequestedAfterInterruptionStop = false;
            mIdleStopRequestedAfterImmediateOperation = false;
            mPendingDeviceRequest = DeviceLifecycleRequest::start;
            ++mDeviceRequestGeneration;
            shouldNotify = true;
            break;

        case DeviceLifecycleRequest::interruptionStop:
            // Highest-priority operation.
            // A new interruption supersedes any recovery start from an older
            // interruption cycle.
            mStartRequestedAfterInterruptionStop = false;
            mIdleStopRequestedAfterImmediateOperation = false;
            mPendingDeviceRequest =
                DeviceLifecycleRequest::interruptionStop;
            ++mDeviceRequestGeneration;
            shouldNotify = true;
            break;

        case DeviceLifecycleRequest::none:
            return false;
        }
    }

    if (shouldNotify)
        mPauseCv.notify_one();

    return true;
#endif
}

void Player::startPauseEngineScheduler()
{
#ifndef __EMSCRIPTEN__
    std::lock_guard<std::mutex> lock(mPauseMutex);
    if (mPauseThreadRunning)
        return;
    mStopPauseThread = false;
    mPendingDeviceRequest = DeviceLifecycleRequest::none;
    mImmediateDeviceRequestInFlight = DeviceLifecycleRequest::none;
    mIdleStopRequestedAfterImmediateOperation = false;
    mStartRequestedAfterInterruptionStop = false;
    ++mDeviceRequestGeneration;
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
        mPendingDeviceRequest = DeviceLifecycleRequest::none;
        mIdleStopRequestedAfterImmediateOperation = false;
        mStartRequestedAfterInterruptionStop = false;
        ++mDeviceRequestGeneration;
    }
    mPauseCv.notify_all();
    if (mPauseThread.joinable())
    {
        mPauseThread.join();
    }
    {
        std::lock_guard<std::mutex> lock(mPauseMutex);
        mPauseThreadRunning = false;
        mImmediateDeviceRequestInFlight = DeviceLifecycleRequest::none;
    }
#endif
}

void Player::pauseEngineScheduler()
{
    while (true)
    {
        std::unique_lock<std::mutex> lock(mPauseMutex);
        mPauseCv.wait(lock, [this] {
            return mPendingDeviceRequest != DeviceLifecycleRequest::none ||
                   mStopPauseThread;
        });
        if (mStopPauseThread)
            break;

        const DeviceLifecycleRequest request = mPendingDeviceRequest;
        const uint64_t requestGeneration = mDeviceRequestGeneration;
        mPendingDeviceRequest = DeviceLifecycleRequest::none;

        // Starts and interruption stops are performed immediately. Mark the
        // operation in flight before releasing mPauseMutex so idle requests
        // cannot replace or invalidate it while the backend call is running.
        if (request == DeviceLifecycleRequest::start ||
            request == DeviceLifecycleRequest::interruptionStop)
        {
            mImmediateDeviceRequestInFlight = request;
            lock.unlock();

            std::lock_guard<std::mutex> operationLock(
                mDeviceLifecycleOperationMutex);

            if (isDeviceRequestCurrent(requestGeneration))
            {
                if (request == DeviceLifecycleRequest::start)
                {
                    // performAudioDeviceStart() has already rebuilt the device
                    // and retried by the time it reports an error, so this is
                    // the end of the automatic recovery path. Nothing above can
                    // return it to the caller -- play()/setPause() completed
                    // long ago -- so publish it as an event instead.
                    if (performAudioDeviceStart() != noError)
                        reportAutomaticDeviceStartFailure();
                }
                else
                {
                    performAudioDeviceStop(true);
                }
            }

            bool shouldNotify = false;

            {
                std::lock_guard<std::mutex> pauseLock(mPauseMutex);

                mImmediateDeviceRequestInFlight =
                    DeviceLifecycleRequest::none;

                if (mStopPauseThread)
                {
                    mStartRequestedAfterInterruptionStop = false;
                    mIdleStopRequestedAfterImmediateOperation = false;
                }
                else
                {
                    const bool interruptionStopPending =
                        mPendingDeviceRequest ==
                        DeviceLifecycleRequest::interruptionStop;

                    if (request == DeviceLifecycleRequest::interruptionStop &&
                        mStartRequestedAfterInterruptionStop &&
                        !mInterruptionActive.load(std::memory_order_acquire) &&
                        !interruptionStopPending)
                    {
                        // The interruption has ended. Run the recovery start
                        // now that the higher-priority stop has completed.
                        mStartRequestedAfterInterruptionStop = false;
                        mPendingDeviceRequest =
                            DeviceLifecycleRequest::start;
                        ++mDeviceRequestGeneration;
                        shouldNotify = true;
                    }
                    else if (mIdleStopRequestedAfterImmediateOperation)
                    {
                        const bool anotherImmediatePending =
                            mPendingDeviceRequest ==
                                DeviceLifecycleRequest::start ||
                            mPendingDeviceRequest ==
                                DeviceLifecycleRequest::interruptionStop;

                        if (!anotherImmediatePending)
                        {
                            mIdleStopRequestedAfterImmediateOperation = false;

                            if (mPendingDeviceRequest !=
                                DeviceLifecycleRequest::idleStop)
                            {
                                mPendingDeviceRequest =
                                    DeviceLifecycleRequest::idleStop;
                                ++mDeviceRequestGeneration;
                            }

                            shouldNotify = true;
                        }
                        // Otherwise leave the deferred flag set. It must run
                        // after the newer immediate operation completes.
                    }
                }
            }

            if (shouldNotify)
                mPauseCv.notify_one();

            continue;
        }

        // An idle-stop request waits for the configured timeout. Any newer
        // request advances the generation and invalidates this deadline. A
        // newer idle-stop restarts the delay; a newer start cancels it.
        const int64_t timeoutMs = mIdleTimeoutMs.load();
        if (timeoutMs < 0)
            continue;
        if (timeoutMs > 0)
        {
            // std::condition_variable may convert its duration to a
            // higher-resolution clock representation. Bound each individual
            // wait so a valid 64-bit millisecond timeout cannot overflow that
            // conversion on platforms whose clock uses 64-bit nanoseconds.
            int64_t remainingMs = timeoutMs;
            while (remainingMs > 0)
            {
                const int64_t waitMs =
                    std::min(remainingMs, kMaxIdleWaitChunkMs);
                const bool interrupted = mPauseCv.wait_for(
                    lock,
                    std::chrono::milliseconds(waitMs),
                    [this, requestGeneration] {
                        return mDeviceRequestGeneration != requestGeneration ||
                               mStopPauseThread;
                    });
                if (interrupted)
                    break;
                remainingMs -= waitMs;
            }
        }

        if (mStopPauseThread)
            break;

        // The request is stale. The replacement intent remains pending and is
        // processed on the next iteration.
        if (mDeviceRequestGeneration != requestGeneration)
            continue;

        lock.unlock();
        std::lock_guard<std::mutex> operationLock(
            mDeviceLifecycleOperationMutex);
        if (isDeviceRequestCurrent(requestGeneration) &&
            mInited.load(std::memory_order_acquire) &&
            mLifecycleRequestsAccepted.load(std::memory_order_acquire) &&
            soloud.getActiveVoiceCount() == 0 &&
            mIdleTimeoutMs.load() >= 0)
        {
            performAudioDeviceStop(false);
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
    double loopingStartAt,
    double loopingEndAt)
{
    handle = 0;

    ActiveSound *sound = findByHash(soundHash);

    if (sound == nullptr)
        return soundHashNotFound;

    BusData *targetBus = nullptr;
    if (busId != 0) {
        auto it = busMap.find(busId);
        if (it == busMap.end())
            return PlayerErrors::busIdNotFound;
        targetBus = &it->second;
    }

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

    // Create paused so the voice cannot render before its handle and initial
    // state have been registered, and so a voice requested `paused` (or a
    // looping voice whose loop region is not set up yet) never produces audio.
    // The requested pause state is applied only after creation succeeds; the
    // output device is started off the UI thread by `resumeEngine()` below.
    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.play(*sound->sound.get(), volume, pan, true, 0);
    }
    else
    {
        newHandle = targetBus->bus.play(*sound->sound.get(), volume, pan, true);
    }

    // `Soloud::play()` returns the invalid-handle sentinel 0 when it could not
    // allocate a voice. Testing the value with `isValidVoiceHandle()` would not
    // be safe: handles are `(voice + 1) | (playIndex << 12)`, so a failure code
    // can collide with the real handle of an existing voice.
    if (newHandle == 0)
        return failedToStartPlayback;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        // A voice the caller asked to start paused is user-paused: the
        // BufferStream buffering logic must not silently unpause it once
        // enough data has arrived.
        sound->handle.push_back({newHandle, MAX_DOUBLE, paused});
    }

    if (looping)
    {
        setLoopPoint(newHandle, loopingStartAt);
        setLoopEndPoint(newHandle, loopingEndAt);
        setLooping(newHandle, true);
    }

    if (!paused)
    {
        soloud.setPause(newHandle, false);
        // Check if this buffer has enough data to be played. Buffer streams may
        // pause themselves again if they do not yet have enough data; in that
        // case no active voice requires device startup.
        if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
            static_cast<SoLoud::BufferStream *>(sound->sound.get())
                ->checkBuffering(0);
    }

    handle = newHandle;

    if (soloud.isValidVoiceHandle(newHandle) &&
        !soloud.getPause(newHandle))
        resumeEngine();

    return PlayerErrors::noError;
}

PlayerErrors Player::playClocked(
    unsigned int soundHash,
    unsigned int &handle,
    double soundTime,
    unsigned int busId,
    float volume,
    float pan)
{
    handle = 0;

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

    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.playClocked(
            soundTime, *sound->sound.get(), volume, pan, 0);
    }
    else
    {
        auto it = busMap.find(busId);
        if (it != busMap.end())
            newHandle = it->second.bus.playClocked(
                soundTime, *sound->sound.get(), volume, pan);
        else
            return PlayerErrors::busIdNotFound;
    }

    // 0 is the invalid-handle sentinel: no voice could be allocated.
    if (newHandle == 0)
        return failedToStartPlayback;

    sound->handle.push_back({newHandle, MAX_DOUBLE, false});
    // Check if this buffer has enough data to be played
    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
    {
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->checkBuffering(0);
    }
    handle = newHandle;

    // Queue the device start rather than performing it inline. The scheduled
    // delay is expressed in *samples* against mStreamTime, which only advances
    // while the device is mixing, so a voice created against a stopped clock
    // keeps its exact sample offset and simply starts counting down once the
    // device runs. Blocking here to start the device first would only shift
    // every schedule by the device-start latency -- and put ma_device_start()
    // back on the calling isolate, which is the #481 stall.
    resumeEngine();

    return PlayerErrors::noError;
}

void Player::setDelaySamples(unsigned int handle, unsigned int samples)
{
    soloud.setDelaySamples(handle, samples);
}

double Player::getStreamTime(unsigned int handle)
{
    return soloud.getStreamTime(handle);
}

void Player::resetStreamTime()
{
    soloud.resetClockedAnchor();
}

double Player::getEngineTime()
{
    return soloud.getEngineTime();
}

double Player::getPlayheadTime()
{
    return soloud.getPlayheadTime();
}

double Player::getOutputLatency()
{
    return soloud.getOutputLatency();
}

bool Player::isRenderAheadEnabled()
{
    return soloud.isRenderAheadEnabled();
}

PlayerErrors Player::playScheduled(
    unsigned int soundHash,
    unsigned int &handle,
    double atTime,
    double duration,
    unsigned int busId,
    float volume,
    float pan)
{
    handle = 0;

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

    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.playScheduled(
            atTime, *sound->sound.get(), volume, pan, 0);
    }
    else
    {
        auto it = busMap.find(busId);
        if (it != busMap.end())
            newHandle = it->second.bus.playScheduled(
                atTime, *sound->sound.get(), volume, pan);
        else
            return PlayerErrors::busIdNotFound;
    }

    // 0 is the invalid-handle sentinel: no voice could be allocated.
    if (newHandle == 0)
        return failedToStartPlayback;

    sound->handle.push_back({newHandle, MAX_DOUBLE, false});
    if (duration > 0.0)
    {
        soloud.scheduleStopAt(newHandle, atTime + duration);
    }
    // Check if this buffer has enough data to be played
    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
    {
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->checkBuffering(0);
    }
    handle = newHandle;

    // Queue the device start rather than performing it inline. The scheduled
    // delay is expressed in *samples* against mStreamTime, which only advances
    // while the device is mixing, so a voice created against a stopped clock
    // keeps its exact sample offset and simply starts counting down once the
    // device runs. Blocking here to start the device first would only shift
    // every schedule by the device-start latency -- and put ma_device_start()
    // back on the calling isolate, which is the #481 stall.
    resumeEngine();

    return PlayerErrors::noError;
}

void Player::stopScheduled(unsigned int handle, double atTime)
{
    soloud.scheduleStopAt(handle, atTime);
}

void Player::fadeScheduled(unsigned int handle, double atTime, float to,
                           double fadeTime, bool thenStop)
{
    soloud.scheduleFadeAt(handle, atTime, to, fadeTime, thenStop);
}

PlayerErrors Player::stop(unsigned int handle)
{
    if (!mInited.load(std::memory_order_acquire) ||
        !mLifecycleRequestsAccepted.load(std::memory_order_acquire))
    {
        return PlayerErrors::backendNotInited;
    }

    if (!isValidHandle(handle))
    {
        // The voice is already gone. Report it, but still re-evaluate whether
        // the device should idle: the previous `void` version ran this on
        // every call, and skipping it here would quietly change the
        // idle-device behaviour when a voice ends between Dart's validity
        // check and this call.
        evaluateAudioDeviceIdle();
        return soundHandleNotFound;
    }

    soloud.stop(handle);
    // After stopping, check if there are any remaining active voices. If no
    // voices are active, let the idle policy stop the audio device so the OS
    // can properly manage the audio session.
    evaluateAudioDeviceIdle();

    return noError;
}

void Player::stopAll()
{
    if (!mInited)
        return;

    // Stop every active voice. SoLoud dispatches the voice-ended callback
    // for each stopped voice (see Soloud::stopVoice_internal), which removes
    // the handle from the internal sounds list and notifies Dart. The loaded
    // sounds are left untouched.
    soloud.stopAll();

    // No voices remain active: pause the audio device to allow the OS
    // to properly manage the audio session.
    pauseEngine();
}

void Player::stopAudioSource(unsigned int soundHash)
{
    if (!mInited)
        return;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        auto it = std::find_if(sounds.begin(), sounds.end(),
                               [soundHash](const std::unique_ptr<ActiveSound> &sound)
                               {
                                   return sound->soundHash == soundHash;
                               });
        if (it == sounds.end())
            return;

        // Stop every voice playing this source. SoLoud dispatches the
        // voice-ended callback for each stopped voice (see
        // Soloud::stopVoice_internal), which removes the handle from the
        // internal sounds list and notifies Dart. The sound itself stays
        // loaded.
        soloud.stopAudioSource(*it->get()->sound);
    }

    // If no voices remain active, pause the audio device to allow the OS
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
            else if (it->get()->soundType == SoundType::TYPE_PULL_BUFFER_STREAM)
            {
                auto *pullStream = static_cast<SoLoud::PullBufferStream *>(it->get()->sound.get());
                if (pullStream != nullptr)
                {
                    pullStream->markForDestruction();
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
    evaluateAudioDeviceIdle();
}

void Player::stopDeviceAndDestroyAllSounds()
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
            else if (sound->soundType == SoundType::TYPE_PULL_BUFFER_STREAM)
            {
                auto *pullStream = static_cast<SoLoud::PullBufferStream *>(sound->sound.get());
                if (pullStream != nullptr)
                {
                    pullStream->markForDestruction();
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

void Player::disposeAllSound()
{
    {
        // Stopping the device is a real backend operation, so serialize it
        // with automatic/explicit lifecycle work and teardown.
        std::lock_guard<std::mutex> operationLock(
            mDeviceLifecycleOperationMutex);
        stopDeviceAndDestroyAllSounds();
    }

    evaluateAudioDeviceIdle();

    // The unconditional soloud.pause() above may have stopped the device. If
    // the app asked for the device to stay alive while idle (indefinite
    // timeout), restart it (off the UI thread) now that the sounds have been
    // destroyed.
    if (mIdleTimeoutMs.load() < 0)
        resumeEngine();
}

void Player::clearDartCallbackRegistrations()
{
    std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
    for (auto &sound : sounds)
    {
        if (sound != nullptr && sound->sound != nullptr)
        {
            if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
            {
                static_cast<SoLoud::BufferStream *>(sound->sound.get())
                    ->clearDartCallbacks();
            }
            else if (sound->soundType == SoundType::TYPE_PULL_BUFFER_STREAM)
            {
                static_cast<SoLoud::PullBufferStream *>(sound->sound.get())
                    ->clearDartCallbacks();
            }
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

double Player::getLoopEndPoint(unsigned int handle)
{
    return soloud.getLoopEndPoint(handle);
}

void Player::setLoopEndPoint(unsigned int handle, double time)
{
    soloud.setLoopEndPoint(handle, time);
}

PlayerErrors Player::textToSpeech(const std::string &textToSpeech, unsigned int &handle)
{
    handle = 0;

    if (!mInited.load(std::memory_order_acquire))
        return backendNotInited;

    const SoLoud::result result = speech.setText(textToSpeech.c_str());
    if (result != SoLoud::SO_NO_ERROR)
        return fromSoLoudError(result);

    // Speech always begins unpaused, but create it paused until its handle is
    // known and registered.
    const SoLoud::handle newHandle = soloud.play(speech, -1.0f, 0.0f, true);
    // 0 is the invalid-handle sentinel: no voice could be allocated. Don't
    // create any bookkeeping for a sound that is not playing.
    if (newHandle == 0)
        return failedToStartPlayback;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        auto activeSpeech = std::make_unique<ActiveSound>();
        activeSpeech->completeFileName = std::string("");
        activeSpeech->soundHash = newHandle;
        activeSpeech->soundType = TYPE_TEXT_TO_SPEECH;
        activeSpeech->filters = std::make_unique<Filters>(
            &soloud, activeSpeech.get(), nullptr);
        activeSpeech->handle.push_back({newHandle, MAX_DOUBLE, false});
        sounds.push_back(std::move(activeSpeech));
    }

    handle = newHandle;
    soloud.setPause(newHandle, false);
    // Start the output device off the UI thread now that the voice is live.
    resumeEngine();
    return noError;
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
    if (!mInited.load(std::memory_order_acquire))
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
    return fromSoLoudError(result);
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
    double loopingStartAt,
    double loopingEndAt)
{
    handle = 0;

    ActiveSound *sound = findByHash(soundHash);
    if (sound == 0)
        return soundHashNotFound;

    BusData *targetBus = nullptr;
    if (busId != 0) {
        auto it = busMap.find(busId);
        if (it == busMap.end())
            return PlayerErrors::busIdNotFound;
        targetBus = &it->second;
    }

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

    // Create paused so the 3D voice and its initial state are fully registered
    // before it can render or request device startup, and so a voice requested
    // `paused` (or a looping voice whose loop region is not set up yet) never
    // produces audio. The output device is started off the UI thread by
    // `resumeEngine()` below.
    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.play3d(
            *sound->sound.get(),
            posX, posY, posZ,
            velX, velY, velZ,
            volume,
            true,
            0);
    }
    else
    {
        newHandle = targetBus->bus.play3d(
            *sound->sound.get(),
            posX, posY, posZ,
            velX, velY, velZ,
            volume,
            true);
    }

    // 0 is the invalid-handle sentinel: no voice could be allocated.
    if (newHandle == 0)
        return failedToStartPlayback;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        // A voice the caller asked to start paused is user-paused: the
        // BufferStream buffering logic must not silently unpause it once
        // enough data has arrived.
        sound->handle.push_back({newHandle, MAX_DOUBLE, paused});
    }

    if (looping)
    {
        setLoopPoint(newHandle, loopingStartAt);
        setLoopEndPoint(newHandle, loopingEndAt);
        setLooping(newHandle, true);
        seek(newHandle, loopingStartAt);
    }

    if (!paused)
    {
        soloud.setPause(newHandle, false);
        // Check if this buffer has enough data to be played. Buffer streams may
        // pause themselves again if they do not yet have enough data; in that
        // case no active voice requires device startup.
        if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
            static_cast<SoLoud::BufferStream *>(sound->sound.get())
                ->checkBuffering(0);
    }

    handle = newHandle;

    if (soloud.isValidVoiceHandle(newHandle) &&
        !soloud.getPause(newHandle))
        resumeEngine();

    return PlayerErrors::noError;
}

PlayerErrors Player::play3dClocked(
    unsigned int soundHash,
    unsigned int &handle,
    double soundTime,
    float posX,
    float posY,
    float posZ,
    float velX,
    float velY,
    float velZ,
    float volume,
    unsigned int busId)
{
    handle = 0;

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

    SoLoud::handle newHandle = 0;
    if (busId == 0)
    {
        newHandle = soloud.play3dClocked(
            soundTime,
            *sound->sound.get(),
            posX, posY, posZ,
            velX, velY, velZ,
            volume,
            0);
    }
    else
    {
        auto it = busMap.find(busId);
        if (it != busMap.end())
            newHandle = it->second.bus.play3dClocked(
                soundTime,
                *sound->sound.get(),
                posX, posY, posZ,
                velX, velY, velZ,
                volume);
        else
            return PlayerErrors::busIdNotFound;
    }

    // 0 is the invalid-handle sentinel: no voice could be allocated.
    if (newHandle == 0)
        return failedToStartPlayback;

    {
        std::lock_guard<std::recursive_mutex> lock(sounds_mutex);
        sound->handle.push_back({newHandle, MAX_DOUBLE, false});
    }
    // Check if this buffer has enough data to be played
    if (sound->soundType == SoundType::TYPE_BUFFER_STREAM)
    {
        static_cast<SoLoud::BufferStream *>(sound->sound.get())->checkBuffering(0);
    }
    handle = newHandle;

    // Queue the device start rather than performing it inline. The scheduled
    // delay is expressed in *samples* against mStreamTime, which only advances
    // while the device is mixing, so a voice created against a stopped clock
    // keeps its exact sample offset and simply starts counting down once the
    // device runs. Blocking here to start the device first would only shift
    // every schedule by the device-start latency -- and put ma_device_start()
    // back on the calling isolate, which is the #481 stall.
    resumeEngine();

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
    evaluateAudioDeviceIdle();
}

PlayerErrors Player::busPlayOnEngine(unsigned int busId, float volume,
                                     bool paused, unsigned int &handle)
{
    handle = 0;

    if (!mInited.load(std::memory_order_acquire))
        return backendNotInited;
    auto it = busMap.find(busId);
    if (it == busMap.end())
        return busIdNotFound;

    // Create paused so the bus handle and initial pan are committed before the
    // bus can render or request device startup.
    SoLoud::handle newHandle = soloud.play(it->second.bus, volume, 0.0f, true);
    // 0 is the invalid-handle sentinel: no voice could be allocated. Never
    // store or configure that value.
    if (newHandle == 0)
        return failedToStartPlayback;

    it->second.handle = newHandle;
    // Playing a sound inside a bus decreases the volume compared to playing it directly.
    // https://github.com/jarikomppa/soloud/issues/395#issuecomment-4148675275
    soloud.setPanAbsolute(newHandle, 1.0f, 1.0f);

    if (!paused)
    {
        soloud.setPause(newHandle, false);
        // Start the output device off the UI thread now that the bus is live.
        resumeEngine();
    }

    handle = newHandle;
    return noError;
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

// Standalone native test: playing a sound streamed from disk (LoadMode.disk)
// whose file has been deleted since it was loaded.
//
// Such a sound keeps only its file's path, and each voice opens the file
// again. The Player now checks the file before it creates the voice and
// returns fileNotFound, which Dart throws as SoLoudFileNotFoundException,
// instead of handing back a voice with nothing to play: it ended at the first
// mix or, looping, stayed silent until it was stopped.
//
// For the repo's WAV, MP3 and FLAC samples, loaded from a temporary copy in
// disk mode, the test checks that once the copy is deleted:
// - play(), playClocked(), playScheduled(), play3d(), play3dWithLoopPoints()
//   (what Dart's play3d() calls), play3dClocked() and play3dScheduled(),
//   looping or not, return fileNotFound and no handle, and no voice is
//   created;
// - a voice already playing another sound plays on;
// - once the file is written back, the sound plays again;
// and that a sound streamed from its bytes in memory, which has no file,
// still plays. When the copy is overwritten after loading with bytes no
// decoder accepts, the file opens, so the voice is created; it ends at once.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_play_missing_file_test.sh            (plain)
//   ./test/run_play_missing_file_test.sh --asan     (ASan and UBSan)
//   ./test/run_play_missing_file_test.sh --tsan     (ThreadSanitizer)

#include "bindings.h"
#include "enums.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern "C"
{
    // Test-only export, compiled in with SOLOUD_LIFECYCLE_TEST_HOOKS.
    int soloudTestSoundCount();
}

namespace
{

constexpr int64_t kEngineId = 4005;
constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kChannels = 2;

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...)                                    \
    do                                                                    \
    {                                                                     \
        ++gAssertions;                                                    \
        if (!(condition))                                                 \
        {                                                                 \
            ++gFailures;                                                  \
            std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", __FILE__, \
                         __LINE__, ##__VA_ARGS__);                        \
        }                                                                 \
    } while (0)

/// Abort a wedged run instead of hanging. macOS has no `timeout` command.
void startHangGuard(int limitSeconds)
{
    std::thread(
        [limitSeconds]
        {
            std::this_thread::sleep_for(std::chrono::seconds(limitSeconds));
            std::fprintf(stderr,
                         "\n  FAIL: hang guard fired after %ds -- the suite is "
                         "wedged.\n",
                         limitSeconds);
            std::fflush(stderr);
            std::fflush(stdout);
            std::_Exit(1);
        })
        .detach();
}

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------

/// The hash and result of the last loadFile(), which reports them through
/// the file-loaded callback before it returns.
unsigned int gLoadedHash = 0;
PlayerErrors gLoadError = PlayerErrors::unknownError;

void onVoiceEnded(unsigned int *handle) { std::free(handle); }

void onFileLoaded(enum PlayerErrors *e, char *name, unsigned int *hash,
                  uint64_t *counter)
{
    gLoadError = *e;
    gLoadedHash = *hash;
    std::free(e);
    std::free(name);
    std::free(hash);
    std::free(counter);
}

void onStateChanged(enum PlayerStateEvents *state) { std::free(state); }

bool bringUpEngine()
{
    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(kEngineId);
    const PlayerErrors err =
        initEngine(-1, kSampleRate, 2048, kChannels, 0, 0, 0);
    if (err != PlayerErrors::noError)
        return false;
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);
    return isInited() != 0;
}

std::vector<char> readAll(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), {});
}

bool writeAll(const std::string &path, const std::vector<char> &bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), (std::streamsize)bytes.size());
    return (bool)out;
}

/// Load [path] in disk mode. 0 on failure.
unsigned int loadFromDisk(const std::string &path)
{
    gLoadedHash = 0;
    gLoadError = PlayerErrors::unknownError;
    std::vector<char> name(path.begin(), path.end());
    name.push_back(0);
    loadFile(name.data(), /*loadIntoMem*/ false, 1);
    return gLoadError == PlayerErrors::noError ? gLoadedHash : 0;
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

enum class Start
{
    play,
    clocked,
    scheduled,
    play3d,
    play3dWithLoopPoints,
    play3dClocked,
    play3dScheduled,
};

constexpr Start kStarts[] = {Start::play,
                             Start::clocked,
                             Start::scheduled,
                             Start::play3d,
                             Start::play3dWithLoopPoints,
                             Start::play3dClocked,
                             Start::play3dScheduled};

const char *name(Start how)
{
    switch (how)
    {
    case Start::play:
        return "play()";
    case Start::clocked:
        return "playClocked()";
    case Start::scheduled:
        return "playScheduled()";
    case Start::play3d:
        return "play3d()";
    case Start::play3dWithLoopPoints:
        return "play3dWithLoopPoints()";
    case Start::play3dClocked:
        return "play3dClocked()";
    case Start::play3dScheduled:
        return "play3dScheduled()";
    }
    return "?";
}

PlayerErrors start(unsigned int hash, Start how, bool looping,
                   unsigned int *handle)
{
    *handle = 0;
    const float volume = 0.05f;
    switch (how)
    {
    case Start::play:
        return play(hash, 0, volume, 0.0f, false, looping, 0.0, 0.0, -1, -1,
                    1.0f, handle);
    case Start::clocked:
        return playClocked(hash, getEngineTime(), 0, volume, 0.0f, 1.0f,
                           looping, 0.0, 0.0, -1, -1, handle);
    case Start::scheduled:
        return playScheduled(hash, getEngineTime(), 0.0, 0, volume, 0.0f, 1.0f,
                             looping, 0.0, 0.0, -1, -1, handle);
    case Start::play3d:
        return play3d(hash, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, volume,
                      false, looping, 0.0, handle);
    case Start::play3dWithLoopPoints:
        return play3dWithLoopPoints(hash, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                    0.0f, volume, false, looping, 0.0, 0.0, -1,
                                    -1, 1.0f, handle);
    case Start::play3dClocked:
        return play3dClocked(hash, getEngineTime(), 0, 1.0f, 0.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, volume, 1.0f, looping, 0.0, 0.0, -1,
                             -1, handle);
    case Start::play3dScheduled:
        return play3dScheduled(hash, getEngineTime(), 0.0, 0, 1.0f, 0.0f, 0.0f,
                               0.0f, 0.0f, 0.0f, volume, 1.0f, looping, 0.0,
                               0.0, -1, -1, handle);
    }
    return PlayerErrors::unknownError;
}

/// Whether [handle] stops being a voice within [seconds].
bool endsWithin(unsigned int handle, double seconds)
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::duration<double>(seconds);
    while (getIsValidVoiceHandle(handle))
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

void testFormat(const std::string &asset, const std::string &workDir,
                unsigned int background)
{
    std::printf("%s\n", asset.c_str());
    const char *what = asset.c_str();
    const std::vector<char> bytes = readAll(asset);
    EXPECT(!bytes.empty(), "%s: should be readable from the repository root",
           what);
    if (bytes.empty())
        return;
    const std::string path =
        workDir + "/stream" + asset.substr(asset.rfind('.'));
    EXPECT(writeAll(path, bytes), "%s: the copy should be written", what);

    const unsigned int hash = loadFromDisk(path);
    EXPECT(hash != 0, "%s: should load from disk (error %d)", what,
           (int)gLoadError);
    if (hash == 0)
        return;

    unsigned int voice = 0;
    PlayerErrors err = start(hash, Start::play, false, &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0,
           "%s: should play before the file is deleted (error %d)", what,
           (int)err);
    stop(voice);

    EXPECT(std::remove(path.c_str()) == 0, "%s: the copy should be deleted",
           what);
    const unsigned int voicesBefore = getVoiceCount();
    for (Start how : kStarts)
    {
        for (bool looping : {false, true})
        {
            err = start(hash, how, looping, &voice);
            EXPECT(err == PlayerErrors::fileNotFound,
                   "%s deleted: %s%s should return fileNotFound, not %d", what,
                   name(how), looping ? " looping" : "", (int)err);
            EXPECT(voice == 0, "%s deleted: %s%s should give no handle", what,
                   name(how), looping ? " looping" : "");
        }
    }
    EXPECT(getVoiceCount() == voicesBefore,
           "%s deleted: no voice should have been created (%u, was %u)", what,
           getVoiceCount(), voicesBefore);
    EXPECT(countAudioSource(hash) == 0,
           "%s deleted: no voice should play it, but %d do", what,
           countAudioSource(hash));
    EXPECT(getIsValidVoiceHandle(background),
           "%s deleted: the background voice should play on", what);

    // Back again.
    EXPECT(writeAll(path, bytes), "%s: the copy should be written back", what);
    for (Start how : kStarts)
    {
        err = start(hash, how, true, &voice);
        EXPECT(err == PlayerErrors::noError && voice != 0,
               "%s written back: %s should play (error %d)", what, name(how),
               (int)err);
        stop(voice);
    }

    // Overwritten: the file opens, so the voice is created, but no decoder
    // accepts it. It ends at once.
    EXPECT(writeAll(path, std::vector<char>(bytes.size(), 0)),
           "%s: the copy should be overwritten", what);
    err = start(hash, Start::play, false, &voice);
    EXPECT(err == PlayerErrors::noError && voice != 0,
           "%s overwritten: play() should give a voice (error %d)", what,
           (int)err);
    EXPECT(endsWithin(voice, 2.0),
           "%s overwritten: the voice should end at once", what);
    stop(voice);

    disposeSound(hash);
    std::remove(path.c_str());
}

/// A sound streamed from its bytes in memory has no file to check.
void testStreamedFromMemory()
{
    std::printf("a sound streamed from its bytes\n");
    std::vector<char> mp3 = readAll("example/assets/audio/sample-MP3.mp3");
    EXPECT(!mp3.empty(), "the MP3 should be readable");
    if (mp3.empty())
        return;
    char soundName[] = "play_missing_file_streamed.mp3";
    unsigned int hash = 0;
    const PlayerErrors loaded =
        loadMem(soundName, reinterpret_cast<unsigned char *>(mp3.data()),
                static_cast<int>(mp3.size()), 0, &hash);
    EXPECT(loaded == PlayerErrors::noError && hash != 0,
           "it should load (error %d)", (int)loaded);
    if (hash == 0)
        return;
    for (Start how : kStarts)
    {
        unsigned int voice = 0;
        const PlayerErrors err = start(hash, how, true, &voice);
        EXPECT(err == PlayerErrors::noError && voice != 0,
               "%s should play it (error %d)", name(how), (int)err);
        stop(voice);
    }
    disposeSound(hash);
}

} // namespace

int main()
{
    startHangGuard(120);

    if (!bringUpEngine())
    {
        if (isInited())
            dispose();
        std::fprintf(stderr,
                     "FATAL: no output device could be opened, not even a null "
                     "backend; the test cannot run.\n"
                     "Set SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE=1 to downgrade "
                     "this to a skip.\n");
        return std::getenv("SOLOUD_LIFECYCLE_TEST_ALLOW_NO_DEVICE") ? 0 : 1;
    }

    const char *tmp = std::getenv("TMPDIR");
    std::string workTemplate =
        std::string(tmp != nullptr && *tmp != 0 ? tmp : "/tmp") +
        "/play_missing_file_XXXXXX";
    const char *workDir = mkdtemp(&workTemplate[0]);
    EXPECT(workDir != nullptr, "a temporary directory should be made");
    if (workDir == nullptr)
    {
        dispose();
        return 1;
    }

    // A voice that plays through the whole test.
    unsigned int backgroundHash = 0;
    loadWaveform(0, false, 1.0f, 1.0f, &backgroundHash);
    unsigned int background = 0;
    play(backgroundHash, 0, 0.01f, 0.0f, false, true, 0.0, 0.0, -1, -1, 1.0f,
         &background);
    EXPECT(background != 0, "the background voice should play");

    for (const char *asset : {"example/assets/audio/sample-WAV.wav",
                              "example/assets/audio/sample-MP3.mp3",
                              "example/assets/audio/sample-FLAC.flac"})
        testFormat(asset, workDir, background);

    testStreamedFromMemory();

    EXPECT(getIsValidVoiceHandle(background),
           "the background voice should still play at the end");
    stop(background);
    disposeSound(backgroundHash);
    rmdir(workDir);

    EXPECT(getVoiceCount() == 0, "%u voice(s) still alive", getVoiceCount());
    EXPECT(soloudTestSoundCount() == 0, "%d sound(s) still loaded",
           soloudTestSoundCount());

    dispose();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

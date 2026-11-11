// Standalone native test: the buffer passed to loadMem() belongs to the
// caller.
//
// Dart allocates it with package:ffi's calloc (CoTaskMemAlloc on Windows),
// copies the encoded bytes into it and frees it as soon as the call returns,
// so the native loader must neither free it nor keep it:
//
//   * a disk-mode stream (WavStream) that parses the bytes must stream from
//     its own copy: it used to keep the caller's buffer, read it while
//     playing and free it with delete[] when the sound was disposed;
//   * a format the stream rejects (M4A, AAC) falls back to the plugin's other
//     decoders, which read the same bytes again, so the rejected stream must
//     not have freed them first.
//
// The test calls the loadMem() export the way Dart does, on a real engine
// (miniaudio falls back to its null backend without audio hardware): for
// each file and load mode it fills a calloc'd buffer, loads it, checks the
// buffer is untouched, frees it, plays the sound for a while and disposes of
// it. The argument picks the files:
//
//   soloud    WAV and MP3, which the SoLoud loaders parse.
//   platform  M4A and AAC, which fall back to the platform decoder. Where the
//             platform cannot decode AAC, the fallback fails cleanly and only
//             the memory checks apply.
//
// A loader that frees or keeps the caller's buffer reads it after it was
// freed or frees it a second time: under AddressSanitizer that aborts the
// run, and without it a plain build usually crashes or reads garbage.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_load_mem_ownership_test.sh          (plain)
//   ./test/run_load_mem_ownership_test.sh --asan   (AddressSanitizer)

#include "bindings.h"
#include "enums.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

extern "C"
{
    // Test-only export, compiled in with SOLOUD_LIFECYCLE_TEST_HOOKS.
    int soloudTestSoundCount();
}

namespace
{

int gAssertions = 0;
int gFailures = 0;

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

void onVoiceEnded(unsigned int *handle) { std::free(handle); }

void onFileLoaded(enum PlayerErrors *error, char *name, unsigned int *hash,
                  uint64_t *counter)
{
    std::free(error);
    std::free(name);
    std::free(hash);
    std::free(counter);
}

void onStateChanged(enum PlayerStateEvents *state) { std::free(state); }

std::vector<unsigned char> readFile(const char *path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    std::vector<unsigned char> data;
    if (!in)
        return data;
    data.resize(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    in.read(reinterpret_cast<char *>(data.data()),
            static_cast<std::streamsize>(data.size()));
    return data;
}

/// Loads [path] the way Dart's loadMem() does, frees the caller's buffer as
/// Dart does once the call returns, then plays the sound for a while and
/// disposes of it. [mustLoad]: whether the load has to succeed here.
void loadLikeDart(const char *path, bool loadIntoMem, bool mustLoad)
{
    const char *mode = loadIntoMem ? "memory" : "disk";
    const std::vector<unsigned char> data = readFile(path);
    EXPECT(!data.empty(), "%s: cannot read it (run from the repository root)",
           path);
    if (data.empty())
        return;

    auto *buffer = static_cast<unsigned char *>(std::calloc(data.size(), 1));
    std::memcpy(buffer, data.data(), data.size());
    std::string name = std::string(path) + "#" + mode;
    unsigned int hash = 0;
    const PlayerErrors error =
        loadMem(&name[0], buffer, static_cast<int>(data.size()),
                loadIntoMem ? 1 : 0, &hash);
    std::printf("  %s, %s mode: error %d\n", path, mode,
                static_cast<int>(error));
    if (mustLoad)
        EXPECT(error == PlayerErrors::noError && hash != 0,
               "%s should load in %s mode (error %d)", path, mode,
               static_cast<int>(error));
    // The loader may read the buffer but not change or free it.
    EXPECT(std::memcmp(buffer, data.data(), data.size()) == 0,
           "%s, %s mode: the caller's buffer changed", path, mode);
    // Dart frees it as soon as loadMem() returns.
    std::free(buffer);

    if (hash == 0)
        return;
    unsigned int voice = 0;
    const PlayerErrors played = play(hash, 0, 0.1f, 0.0f, false, true, 0.0,
                                     0.0, -1, -1, 1.0f, &voice);
    EXPECT(played == PlayerErrors::noError && voice != 0,
           "%s, %s mode: should play (error %d)", path, mode,
           static_cast<int>(played));
    // Let the mixer read the stream for a few blocks.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT(getIsValidVoiceHandle(voice),
           "%s, %s mode: the looping voice should still play", path, mode);
    disposeSound(hash);
}

} // namespace

int main(int argc, char **argv)
{
    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(4018);
    if (initEngine(-1, 44100, 2048, 2, 0, 0, 0) != PlayerErrors::noError)
    {
        std::fprintf(stderr, "FATAL: the engine did not start, not even on "
                             "the null backend\n");
        return 1;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 4018);

#ifdef __APPLE__
    const bool platformDecodesAac = true; // AudioToolbox
#else
    const bool platformDecodesAac = false;
#endif
    const bool soloudFormats =
        argc < 2 || std::strcmp(argv[1], "platform") != 0;
    const bool platformFormats =
        argc < 2 || std::strcmp(argv[1], "soloud") != 0;
    for (const bool loadIntoMem : {false, true})
    {
        if (soloudFormats)
        {
            loadLikeDart("example/assets/audio/sample-WAV.wav", loadIntoMem,
                         true);
            loadLikeDart("example/assets/audio/sample-MP3.mp3", loadIntoMem,
                         true);
        }
        if (platformFormats)
        {
            loadLikeDart("example/assets/audio/sample-AAC.m4a", loadIntoMem,
                         platformDecodesAac);
            loadLikeDart("example/assets/audio/sample-AAC.aac", loadIntoMem,
                         platformDecodesAac);
        }
    }

    EXPECT(soloudTestSoundCount() == 0, "%d sound(s) still loaded",
           soloudTestSoundCount());
    dispose();
    std::printf("%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

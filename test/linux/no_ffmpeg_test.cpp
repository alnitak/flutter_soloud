// Native test: the plugin on a Linux machine without FFmpeg.
//
// AAC, M4A/MP4, AC-3 and E-AC-3 are decoded on Linux by the system FFmpeg,
// which src/native_decoder/os_decoder_linux.cpp and
// src/audiobuffer/aac_stream_decoder_linux.cpp dlopen() at run time. Without
// it the plugin must fail those loads cleanly and keep everything else
// working. The test links the whole plugin and goes through the same C entry
// points as Dart (loadFile, loadMem, setBufferStream/addAudioDataStream).
//
// FFmpeg is either really absent (the `none` image of test/docker/run.sh
// ffmpeg) or hidden: this file defines dlopen(), which the decoders linked
// into this executable call instead of libc's, and with NO_FFMPEG_HIDE=1 it
// refuses libav* / libswresample names.
//
// Checks:
//   1. M4A, MP4, ADTS AAC, AC-3, E-AC-3 through loadMem and loadFile, with
//      loadIntoMem true and false: an error, never a crash or a "loaded"
//      sound; the error says the format is not supported here
//      (audioFormatNotSupported), as the streaming path already does, not
//      the generic fileLoadFailed of a corrupt file.
//   2. An AAC buffer stream (BufferType AUTO) reports audioFormatNotSupported
//      from addAudioDataStream().
//   3. MP3, WAV and FLAC still load (memory and disk mode) and play.
//
//   no_ffmpeg_test <asset dir> [--strict]

#include "bindings.h"
#include "enums.h"

#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// dlopen() interposer (same technique as os_decoder_linux_test.cpp).

static bool hideFfmpeg()
{
    const char *v = std::getenv("NO_FFMPEG_HIDE");
    return v != nullptr && v[0] == '1';
}
static std::atomic<int> gHiddenOpens{0};

#if defined(__GLIBC__)
#define DLOPEN_NOEXCEPT noexcept(true)
#else
#define DLOPEN_NOEXCEPT
#endif

static bool isFfmpegName(const char *file)
{
    return std::strstr(file, "libavformat") != nullptr ||
           std::strstr(file, "libavcodec") != nullptr ||
           std::strstr(file, "libavutil") != nullptr ||
           std::strstr(file, "libswresample") != nullptr;
}

extern "C" void *dlopen(const char *file, int mode) DLOPEN_NOEXCEPT
{
    using DlopenFn = void *(*)(const char *, int);
    static DlopenFn real =
        reinterpret_cast<DlopenFn>(dlsym(RTLD_NEXT, "dlopen"));
    if (file != nullptr && hideFfmpeg() && isFfmpegName(file))
    {
        ++gHiddenOpens;
        return nullptr;
    }
    return real != nullptr ? real(file, mode) : nullptr;
}

// ---------------------------------------------------------------------------

static int gChecks = 0, gFailures = 0, gKnownBugs = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

#define KNOWN_BUG(id, cond, ...)                                               \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            if (gStrict) ++gFailures; else ++gKnownBugs;                       \
            std::printf("  %s %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",\
                        id, __FILE__, __LINE__, #cond);                        \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

// loadFile() reports through the file-loaded callback.
static std::mutex gLoadMutex;
static bool gLoadDone = false;
static PlayerErrors gLoadError = noError;
static unsigned gLoadHash = 0;

static void onVoiceEnded(unsigned int *h) { std::free(h); }
static void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                         uint64_t *c)
{
    {
        std::lock_guard<std::mutex> lock(gLoadMutex);
        gLoadDone = true;
        gLoadError = e ? *e : unknownError;
        gLoadHash = h ? *h : 0;
    }
    std::free(e);
    std::free(n);
    std::free(h);
    std::free(c);
}
static void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

static std::vector<unsigned char> readAll(const std::string &path)
{
    std::ifstream f(path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

static PlayerErrors viaLoadMem(const std::string &path, bool intoMem,
                               unsigned &hash)
{
    std::vector<unsigned char> bytes = readAll(path);
    hash = 0;
    if (bytes.empty())
        return fileNotFound;
    std::string name = path + (intoMem ? "#mem" : "#stream");
    return loadMem(&name[0], bytes.data(), (int)bytes.size(), intoMem ? 1 : 0,
                   &hash);
}

static PlayerErrors viaLoadFile(const std::string &path, bool intoMem,
                                unsigned &hash)
{
    {
        std::lock_guard<std::mutex> lock(gLoadMutex);
        gLoadDone = false;
    }
    std::string p = path;
    loadFile(&p[0], intoMem, 0);
    std::lock_guard<std::mutex> lock(gLoadMutex);
    hash = gLoadDone ? gLoadHash : 0;
    return gLoadDone ? gLoadError : unknownError;
}

static const char *modeName(bool intoMem) { return intoMem ? "memory" : "disk"; }

static void testOsFormatsFailCleanly(const std::string &dir)
{
    std::printf("OS-decoded formats fail cleanly without FFmpeg\n");
    const char *files[] = {"aac_lc_stereo_48000.m4a", "sample-AAC.m4a",
                           "sample-AAC.mp4", "sample-AAC.aac",
                           "sample-AC3.ac3", "sample-EAC3.eac3"};
    for (const char *f : files)
    {
        const std::string path = dir + "/" + f;
        for (int api = 0; api < 2; api++)
        {
            for (bool intoMem : {true, false})
            {
                unsigned hash = 0;
                const PlayerErrors err = api == 0
                                             ? viaLoadMem(path, intoMem, hash)
                                             : viaLoadFile(path, intoMem, hash);
                const char *apiName = api == 0 ? "loadMem" : "loadFile";
                std::printf("  %-24s %-8s %-6s -> %d\n", f, apiName,
                            modeName(intoMem), err);
                CHECK(err != noError && err != fileAlreadyLoaded,
                      "%s(%s, %s) must fail without FFmpeg", apiName, f,
                      modeName(intoMem));
                CHECK(hash == 0 || err != noError,
                      "no sound registered for %s", f);
                if (err == noError || err == fileAlreadyLoaded)
                {
                    if (hash != 0)
                        disposeSound(hash);
                    continue;
                }
                CHECK(err == audioFormatNotSupported,
                      "%s(%s, %s) returned %d (%s); without FFmpeg the "
                      "format is unsupported here, which the streaming "
                      "path reports as audioFormatNotSupported (29)",
                      apiName, f, modeName(intoMem), err,
                      err == fileLoadFailed ? "fileLoadFailed, same as a "
                                              "corrupt file"
                                            : "other");
            }
        }
    }
}

static void testAacStreamFailsCleanly(const std::string &dir)
{
    std::printf("AAC buffer stream without FFmpeg\n");
    std::vector<unsigned char> aac = readAll(dir + "/sample-AAC.aac");
    CHECK(!aac.empty(), "fixture sample-AAC.aac");
    unsigned hash = 0;
    const PlayerErrors setup = setBufferStream(
        &hash, 4 * 1024 * 1024, /*preserved*/ 0, 0.0, 44100, 2, AUTO, nullptr,
        nullptr);
    CHECK(setup == noError, "AUTO stream setup (%d)", setup);
    if (setup != noError)
        return;
    const size_t chunk = aac.size() < 16384 ? aac.size() : 16384;
    const PlayerErrors add =
        addAudioDataStream(hash, aac.data(), (unsigned)chunk);
    std::printf("  addAudioDataStream(AAC) -> %d\n", add);
    CHECK(add == audioFormatNotSupported,
          "AAC data without FFmpeg -> audioFormatNotSupported, got %d", add);
    disposeSound(hash);
}

static void testOtherFormatsWork(const std::string &dir)
{
    std::printf("bundled formats still work\n");
    // Ogg Vorbis/Opus need the Xiph libraries, which this build leaves out
    // (NO_XIPH_LIBS) so it also runs where they are not installed.
    const char *files[] = {"sample-MP3.mp3", "sample-WAV.wav",
                           "sample-FLAC.flac", "explosion.mp3"};
    for (const char *f : files)
    {
        const std::string path = dir + "/" + f;
        for (int api = 0; api < 2; api++)
        {
            for (bool intoMem : {true, false})
            {
                unsigned hash = 0;
                const PlayerErrors err = api == 0
                                             ? viaLoadMem(path, intoMem, hash)
                                             : viaLoadFile(path, intoMem, hash);
                const char *apiName = api == 0 ? "loadMem" : "loadFile";
                CHECK((err == noError || err == fileAlreadyLoaded) && hash != 0,
                      "%s(%s, %s) -> %d", apiName, f, modeName(intoMem), err);
                if (hash == 0)
                    continue;
                const double len = getLength(hash);
                CHECK(len > 0.5, "%s length %.2f s", f, len);
                unsigned voice = 0;
                CHECK(play(hash, 0, 0.1f, 0, false, false, 0, 0, -1, -1, 1.0f,
                           &voice) == noError &&
                          getIsValidVoiceHandle(voice),
                      "%s plays", f);
                std::this_thread::sleep_for(std::chrono::milliseconds(60));
                CHECK(getStreamTime(voice) > 0.0,
                      "%s (%s, %s) advances: stream time %.3f", f, apiName,
                      modeName(intoMem), getStreamTime(voice));
                stop(voice);
                disposeSound(hash);
            }
        }
        std::printf("  %s: ok\n", f);
    }
}

int main(int argc, char **argv)
{
    std::string dir;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else dir = argv[i];
    }
    if (dir.empty())
    {
        std::printf("usage: no_ffmpeg_test <asset dir> [--strict]\n");
        return 2;
    }

    // Make sure FFmpeg really is unreachable, through the same dlopen() the
    // decoders use.
    const char *probe[] = {"libavformat.so.63", "libavformat.so.62",
                           "libavformat.so.61", "libavformat.so.60",
                           "libavformat.so.59", "libavformat.so.58",
                           "libavformat.so", nullptr};
    for (int i = 0; probe[i]; i++)
    {
        void *h = dlopen(probe[i], RTLD_NOW | RTLD_LOCAL);
        if (h != nullptr)
        {
            std::printf("FAIL: %s loads; run with NO_FFMPEG_HIDE=1 or on a "
                        "machine without FFmpeg\n",
                        probe[i]);
            return 1;
        }
    }
    std::printf("FFmpeg %s\n", hideFfmpeg() ? "hidden (NO_FFMPEG_HIDE=1)"
                                            : "not installed");

    prepareEngineInit(1);
    const PlayerErrors init = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    if (init != noError)
    {
        std::printf("FAIL: engine init (null backend) returned %d\n", init);
        return 1;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);

    testOsFormatsFailCleanly(dir);
    testAacStreamFailsCleanly(dir);
    testOtherFormatsWork(dir);

    dispose();
    std::printf("%d checks, %d failed, %d known bugs\n", gChecks, gFailures,
                gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}

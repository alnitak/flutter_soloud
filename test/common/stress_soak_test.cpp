// Long-running randomized soak of the whole native engine (TEST_PLAN X6).
//
// Links bindings.cpp, player.cpp and SoLoud with miniaudio's null backend
// (device-free) and, for a fixed time, applies random operations from a
// seeded generator through the same C entry points Dart uses: load (WAV
// bytes generated here, memory and stream mode; waveforms), play (also
// paused, looping, on a bus), stop, seek, pause, speed, volume fades, filters
// on sounds / buses / the global chain, bus create/destroy, dispose while
// playing, stopAll.
//
// Every few seconds the engine is brought back to a clean state (everything
// stopped and disposed) and measured:
//   - heap in use (mallinfo2 on glibc, malloc zones on macOS, private bytes
//     on Windows): no growth from the first clean checkpoint to the last
//     beyond a small allowance (caches warm up for a while: ~30 KiB over
//     two minutes on Linux). Sanitized builds only report it (RSS) and leave
//     leaks to LSan;
//   - leftover voices: getActiveVoiceCount() and getVoiceCount() are 0;
//   - engine time: advances with wall time (the null backend runs in real
//     time) and never goes backwards.
//
//   stress_soak_test [--seconds N] [--seed S] [--only lpvfdba] [--verbose]
//
// Default 10 s. SOLOUD_STRESS_SECONDS=600 (or --seconds 600) runs the ten
// minute soak. The seed is printed so a failure can be replayed.

#include "bindings.h"
#include "enums.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#elif defined(__GLIBC__)
#include <malloc.h>
#endif
#if defined(__linux__)
#include <unistd.h>
#endif

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define STRESS_SANITIZED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define STRESS_SANITIZED 1
#endif

static int gChecks = 0, gFailures = 0;
static bool gVerbose = false;

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

// Bytes of heap in use (or the closest the platform offers).
static long long heapInUse(const char **how)
{
#if defined(STRESS_SANITIZED) && defined(__linux__)
    *how = "RSS";
    long pages = 0, resident = 0;
    if (FILE *f = std::fopen("/proc/self/statm", "r"))
    {
        if (std::fscanf(f, "%ld %ld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return (long long)resident * sysconf(_SC_PAGESIZE);
#elif defined(__APPLE__)
    *how = "malloc zones";
    malloc_statistics_t st{};
    malloc_zone_statistics(nullptr, &st);
    return (long long)st.size_in_use;
#elif defined(_WIN32)
    *how = "private bytes";
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    K32GetProcessMemoryInfo(GetCurrentProcess(),
                            reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
                            sizeof(pmc));
    return (long long)pmc.PrivateUsage;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    *how = "mallinfo2";
    struct mallinfo2 mi = mallinfo2();
    return (long long)mi.uordblks + (long long)mi.hblkhd;
#else
    *how = "unavailable";
    return 0;
#endif
}

static void onVoiceEnded(unsigned int *h) { std::free(h); }
static void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                         uint64_t *c)
{
    std::free(e);
    std::free(n);
    std::free(h);
    std::free(c);
}
static void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

// A 16-bit PCM WAV of a sine.
static std::vector<unsigned char> makeWav(unsigned rate, unsigned channels,
                                          double seconds, double freq)
{
    const unsigned frames = (unsigned)(rate * seconds);
    const unsigned dataBytes = frames * channels * 2;
    std::vector<unsigned char> b(44 + dataBytes);
    auto u32 = [&](size_t at, uint32_t v) {
        for (int i = 0; i < 4; i++) b[at + i] = (v >> (8 * i)) & 0xff;
    };
    auto u16 = [&](size_t at, uint16_t v) {
        b[at] = v & 0xff;
        b[at + 1] = v >> 8;
    };
    std::memcpy(&b[0], "RIFF", 4);
    u32(4, 36 + dataBytes);
    std::memcpy(&b[8], "WAVEfmt ", 8);
    u32(16, 16);
    u16(20, 1);
    u16(22, (uint16_t)channels);
    u32(24, rate);
    u32(28, rate * channels * 2);
    u16(32, (uint16_t)(channels * 2));
    u16(34, 16);
    std::memcpy(&b[36], "data", 4);
    u32(40, dataBytes);
    for (unsigned i = 0; i < frames; i++)
    {
        const int16_t s = (int16_t)(std::sin(2 * 3.14159265358979323846 * freq * i / rate) * 8000);
        for (unsigned c = 0; c < channels; c++)
            u16(44 + (i * channels + c) * 2, (uint16_t)s);
    }
    return b;
}

struct Soak
{
    std::mt19937 rng;
    std::vector<unsigned> sounds;
    std::vector<unsigned> handles;
    std::vector<unsigned> buses;
    unsigned nameCounter = 0;
    long long ops = 0, opErrors = 0, playErrors = 0;

    // Operation groups enabled (--only): l load, p play, v voice controls
    // (stop/seek/pause/speed/volume/looping), f filters, d dispose, b buses,
    // a stopAll. All by default.
    std::string only = "lpvfdba";
    bool on(char g) const { return only.find(g) != std::string::npos; }

    explicit Soak(unsigned seed) : rng(seed) {}

    int pick(int n) { return std::uniform_int_distribution<int>(0, n - 1)(rng); }
    float uni(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); }

    unsigned anySound() { return sounds.empty() ? 0 : sounds[pick((int)sounds.size())]; }
    unsigned anyHandle() { return handles.empty() ? 0 : handles[pick((int)handles.size())]; }
    unsigned anyBus() { return buses.empty() ? 0 : buses[pick((int)buses.size())]; }

    void load()
    {
        if (sounds.size() >= 12) return;
        unsigned hash = 0;
        PlayerErrors e;
        if (pick(4) == 0)
        {
            e = loadWaveform(pick(9), pick(2) == 0, 1.0f, 0.1f, &hash);
        }
        else
        {
            const unsigned rates[] = {22050, 44100, 48000};
            std::vector<unsigned char> wav =
                makeWav(rates[pick(3)], 1 + pick(2), uni(0.05f, 1.5f),
                        uni(100, 2000));
            std::string name = "soak_" + std::to_string(nameCounter++) + ".wav";
            e = loadMem(&name[0], wav.data(), (int)wav.size(), pick(2), &hash);
        }
        if (e == noError && hash != 0) sounds.push_back(hash);
        else opErrors++;
    }

    void playOne()
    {
        const unsigned s = anySound();
        if (s == 0) return;
        unsigned h = 0;
        const unsigned bus = pick(3) == 0 ? anyBus() : 0;
        const PlayerErrors e = play(s, bus, uni(0.0f, 0.5f), uni(-1, 1),
                                    pick(5) == 0, pick(3) == 0, 0.0, 0.0, -1,
                                    -1, 1.0f, &h);
        if (e != noError && e != maxActiveVoiceCountReached)
        {
            playErrors++;
            if (playErrors <= 5)
                std::printf("  play(sound %u, bus %u) -> %d\n", s, bus, e);
        }
        if (e == noError && h != 0)
        {
            handles.push_back(h);
            if (handles.size() > 64) handles.erase(handles.begin());
        }
    }

    void disposeOne()
    {
        if (sounds.empty()) return;
        const int i = pick((int)sounds.size());
        disposeSound(sounds[i]);
        sounds.erase(sounds.begin() + i);
    }

    void busOp()
    {
        if (buses.size() < 3 && pick(2) == 0)
        {
            const unsigned b = createBus();
            if (b == 0) { opErrors++; return; }
            unsigned h = 0;
            busPlayOnEngine(b, 0.5f, false, &h);
            buses.push_back(b);
        }
        else if (!buses.empty())
        {
            const int i = pick((int)buses.size());
            destroyBus(buses[i]);
            buses.erase(buses.begin() + i);
        }
    }

    void filterOp()
    {
        // Custom (needs a callback) left out.
        const FilterType f = (FilterType)pick((int)CustomFilter);
        unsigned sound = 0, bus = 0;
        switch (pick(3))
        {
        case 0: sound = anySound(); if (sound == 0) return; break;
        case 1: bus = anyBus(); if (bus == 0) return; break;
        default: break; // global
        }
        if (pick(2) == 0) addFilter(sound, bus, f);
        else removeFilter(sound, bus, f);
    }

    void step()
    {
        ops++;
        const unsigned h = anyHandle();
        switch (pick(16))
        {
        case 0: case 1: if (on('l')) load(); break;
        case 2: case 3: case 4: if (on('p')) playOne(); break;
        case 5: if (h && on('v')) stop(h); break;
        case 6: if (h && on('v')) seek(h, uni(0.0f, 1.0f)); break;
        case 7: if (h && on('v')) setPause(h, pick(2) == 0); break;
        case 8: if (h && on('v')) setRelativePlaySpeed(h, uni(0.25f, 3.0f)); break;
        case 9: if (h && on('v')) fadeVolume(h, uni(0, 0.5f), uni(0.0f, 0.3f)); break;
        case 10: if (h && on('v')) setVolume(h, uni(0, 0.5f)); break;
        case 11: if (on('f')) filterOp(); break;
        case 12: if (on('d')) disposeOne(); break;
        case 13: if (on('b')) busOp(); break;
        case 14:
            if (on('a') && pick(20) == 0)
            {
                stopAll();
                // stopAll() also stops the bus voices: play them again.
                for (unsigned b : buses)
                {
                    unsigned bh = 0;
                    busPlayOnEngine(b, 0.5f, false, &bh);
                }
            }
            break;
        case 15: if (h && on('v')) setLooping(h, pick(2) == 0); break;
        }
    }

    // Everything stopped and released: the state memory is compared in.
    void cleanUp()
    {
        stopAll();
        for (unsigned b : buses) destroyBus(b);
        buses.clear();
        disposeAllSound();
        sounds.clear();
        handles.clear();
        for (int f = 0; f < (int)CustomFilter; f++)
            removeFilter(0, 0, (FilterType)f);
    }
};

int main(int argc, char **argv)
{
    double seconds = 10;
    unsigned seed = 20261120;
    std::string only = "lpvfdba";
    if (const char *env = std::getenv("SOLOUD_STRESS_SECONDS"))
        seconds = std::atof(env);
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (unsigned)std::strtoul(argv[++i], nullptr, 10);
        else if (std::strcmp(argv[i], "--verbose") == 0)
            gVerbose = true;
        else if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc)
            only = argv[++i];
    }
    if (seconds < 3) seconds = 3;
    std::printf("soak: %.0f s, seed %u, operations '%s'\n", seconds, seed,
                only.c_str());

    prepareEngineInit(1);
    const PlayerErrors init = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    if (init != noError)
    {
        std::printf("FAIL: engine init (null backend) returned %d\n", init);
        return 1;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);
    setGlobalVolume(0.1f);

    Soak soak(seed);
    soak.only = only;
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const double checkpointEvery = std::max(1.0, std::min(30.0, seconds / 6));
    double nextCheckpoint = checkpointEvery;
    const double startEngine = getEngineTime();
    double lastEngine = startEngine;
    long long firstHeap = -1, lastHeap = 0, peakHeap = 0;
    const char *how = "";
    int checkpoints = 0;
    unsigned maxVoices = 0;

    for (;;)
    {
        const double elapsed =
            std::chrono::duration<double>(clock::now() - start).count();
        const bool last = elapsed >= seconds;
        if (elapsed >= nextCheckpoint || last)
        {
            soak.cleanUp();
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            const unsigned active = getActiveVoiceCount();
            const unsigned voices = getVoiceCount();
            const double engine = getEngineTime();
            const long long heap = heapInUse(&how);
            checkpoints++;
            CHECK(active == 0 && voices == 0,
                  "checkpoint %d: %u active / %u voices left after stopAll + "
                  "disposeAllSound",
                  checkpoints, active, voices);
            CHECK(engine >= lastEngine, "engine time went backwards: %.3f -> %.3f",
                  lastEngine, engine);
            lastEngine = engine;
            // The first checkpoint is the baseline: the allocator, caches
            // and thread stacks have warmed up by then.
            if (firstHeap < 0) firstHeap = heap;
            lastHeap = heap;
            peakHeap = std::max(peakHeap, heap);
            if (gVerbose || last)
                std::printf("  t=%5.1fs ops=%lld heap(%s)=%lld KiB engine=%.2fs\n",
                            elapsed, soak.ops, how, heap / 1024, engine);
            nextCheckpoint += checkpointEvery;
            if (last) break;
        }
        // A burst of operations, then let the mixer run.
        for (int i = 0; i < 20; i++) soak.step();
        maxVoices = std::max(maxVoices, getActiveVoiceCount());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    const double wall = std::chrono::duration<double>(clock::now() - start).count();
    const double engineAdvance = lastEngine - startEngine;
    std::printf("%lld operations, %d checkpoints, up to %u voices at once\n",
                soak.ops, checkpoints, maxVoices);
    std::printf("engine time advanced %.1f s in %.1f s of wall time\n",
                engineAdvance, wall);
#if defined(STRESS_SANITIZED)
    // Under ASan the heavier filters cannot mix in real time on every
    // machine; the clock must still move.
    CHECK(engineAdvance > 0.5, "engine time advanced only %.2f s", engineAdvance);
#else
    CHECK(engineAdvance > wall * 0.5 && engineAdvance < wall * 1.5,
          "engine time follows wall time (%.1f s vs %.1f s)", engineAdvance, wall);
#endif
    CHECK(soak.playErrors == 0,
          "%lld play() calls failed with something other than "
          "maxActiveVoiceCountReached",
          soak.playErrors);
    if (soak.on('p') && soak.on('l'))
        CHECK(maxVoices > 0, "voices were actually playing");
    if (firstHeap > 0)
    {
        const long long growth = lastHeap - firstHeap;
        // An allowance for allocator fragmentation; a leak in a path the soak
        // hits does thousands of times grows far beyond it.
        const long long allowance = 4 * 1024 * 1024 + firstHeap / 10;
        std::printf("heap (%s): first checkpoint %lld KiB, last %lld KiB, "
                    "peak %lld KiB, growth %lld KiB (allowed %lld KiB)\n",
                    how, firstHeap / 1024, lastHeap / 1024, peakHeap / 1024,
                    growth / 1024, allowance / 1024);
#if defined(STRESS_SANITIZED)
        // ASan's quarantine and TSan's shadow memory keep freed memory
        // resident, so RSS says nothing about leaks there (LSan does).
        std::printf("  (sanitized build: growth reported, not checked)\n");
#else
        CHECK(growth <= allowance, "heap grew by %lld KiB", growth / 1024);
#endif
    }

    dispose();
    CHECK(!isInited(), "dispose()");
    std::printf("%d checks, %d failed (seed %u)\n", gChecks, gFailures, seed);
    return gFailures == 0 ? 0 : 1;
}

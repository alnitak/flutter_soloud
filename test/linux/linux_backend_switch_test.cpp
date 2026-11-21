// Native tests for switching the Linux audio backend while the engine runs:
// setLinuxAudioBackend() in src/bindings.cpp -> Player::setLinuxAudioBackend()
// -> miniaudio_changeLinuxBackend_impl() in
// src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp, which tears down the
// device and the miniaudio context and opens new ones on the chosen backend.
//
// The test links the whole plugin (bindings.cpp, player.cpp, SoLoud with the
// real miniaudio backends) and drives it through the same C entry points Dart
// uses. It defines soloud_platform_log() itself (instead of linking
// src/soloud_common.cpp) to read which backend miniaudio actually opened.
//
// Cases:
//   1. ALSA <-> PulseAudio <-> Auto while a voice plays: each switch succeeds,
//      the log names the requested backend, the voice survives and the engine
//      clock keeps advancing (the new device really runs).
//   2. Switching to a backend that is not available (JACK with no server, or
//      no libjack):
//      the call reports an error and the engine keeps playing on the previous
//      backend.
//   3. After that failure, switching back to a working backend recovers.
//   4. Repeated quick switches (stress), then a clean dispose().
//
//   test/linux/linux_backend_switch_test.sh [--asan|--tsan] [--strict]

#include "enums.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>

extern "C"
{
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency,
                                 unsigned int devicePeriodFrames,
                                 unsigned int renderAheadFrames);
    void dispose();
    int isInited();
    enum PlayerErrors setLinuxAudioBackend(unsigned int backend);
    enum PlayerErrors loadWaveform(int waveform, bool superWave, float scale,
                                   float detune, unsigned int *hash);
    enum PlayerErrors play(unsigned int soundHash, unsigned int busId,
                           float volume, float pan, bool paused, bool looping,
                           double loopingStartAt, double loopingEndAt,
                           int loopingStartOffsetAt, int loopingEndOffsetAt,
                           float scale, unsigned int *handle);
    int getIsValidVoiceHandle(unsigned int handle);
    double getEngineTime();
    enum AudioDeviceState getAudioDeviceState();
    void setDartEventCallback(void (*voice_ended)(unsigned int *),
                              void (*file_loaded)(enum PlayerErrors *, char *,
                                                  unsigned int *, uint64_t *),
                              void (*state_changed)(enum PlayerStateEvents *),
                              int64_t owner_engine_id);
}

// ---------------------------------------------------------------------------
// Log capture: replaces src/soloud_common.cpp.

static std::mutex gLogMutex;
static std::string gLastBackend; // from "... Linux backend: <name>"
static std::atomic<bool> gVerbose{false};

void soloud_platform_log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (gVerbose.load())
        std::fputs(buf, stdout);
    const char *tag = std::strstr(buf, "Linux backend: ");
    if (tag != nullptr)
    {
        std::string name(tag + std::strlen("Linux backend: "));
        while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
            name.pop_back();
        std::lock_guard<std::mutex> lock(gLogMutex);
        gLastBackend = name;
    }
}

static std::string lastBackend()
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    return gLastBackend;
}
static void clearLastBackend()
{
    std::lock_guard<std::mutex> lock(gLogMutex);
    gLastBackend.clear();
}

// ---------------------------------------------------------------------------

static int gChecks = 0;
static int gFailures = 0;
static int gKnownBugs = 0;
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

enum Backend : unsigned { kAuto = 0, kAlsa = 1, kPulse = 2, kJack = 3 };
static const char *backendName(unsigned b)
{
    switch (b)
    {
    case kAlsa: return "ALSA";
    case kPulse: return "PulseAudio";
    case kJack: return "JACK";
    default: return "Auto";
    }
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

// True when the engine clock advances by at least `minSeconds` within
// `budgetMs`: the device under the engine is really pulling audio.
static bool engineClockRuns(double minSeconds = 0.1, int budgetMs = 3000)
{
    const double start = getEngineTime();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(budgetMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (getEngineTime() - start >= minSeconds)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static bool libraryLoads(const char *name)
{
    void *h = dlopen(name, RTLD_NOW | RTLD_LOCAL);
    if (h == nullptr)
        return false;
    dlclose(h);
    return true;
}

// miniaudio's backend names, as ma_get_backend_name() spells them.
static bool backendMatches(unsigned requested, const std::string &opened)
{
    if (requested == kAlsa) return opened == "ALSA";
    if (requested == kPulse) return opened == "PulseAudio";
    if (requested == kJack) return opened == "JACK";
    return opened == "ALSA" || opened == "PulseAudio" || opened == "JACK";
}

static void testSwitchesWhilePlaying(unsigned handle)
{
    std::printf("switch ALSA <-> PulseAudio <-> Auto while playing\n");
    const unsigned sequence[] = {kAlsa, kPulse, kAuto, kPulse, kAlsa, kAuto};
    for (unsigned b : sequence)
    {
        clearLastBackend();
        const PlayerErrors err = setLinuxAudioBackend(b);
        CHECK(err == noError, "switch to %s returned %d", backendName(b), err);
        const std::string opened = lastBackend();
        CHECK(backendMatches(b, opened), "asked for %s, miniaudio opened '%s'",
              backendName(b), opened.c_str());
        CHECK(getIsValidVoiceHandle(handle) != 0,
              "the voice survived the switch to %s", backendName(b));
        CHECK(engineClockRuns(),
              "engine clock advances after switching to %s (device state %d)",
              backendName(b), getAudioDeviceState());
        std::printf("  %s -> opened '%s', device state %d\n", backendName(b),
                    opened.c_str(), getAudioDeviceState());
    }
}

static void testUnavailableBackend(unsigned handle, unsigned previous)
{
    std::printf("switch to an unavailable backend (JACK, no JACK server)\n");
    clearLastBackend();
    const PlayerErrors err = setLinuxAudioBackend(kJack);
    if (err == noError)
    {
        std::printf("  not run: JACK works here ('%s')\n", lastBackend().c_str());
        setLinuxAudioBackend(previous);
        return;
    }
    std::printf("  libjack %s\n", (libraryLoads("libjack.so.0") ||
                                     libraryLoads("libjack.so"))
                                        ? "installed, no server"
                                        : "not installed");
    std::printf("  JACK -> error %d, device state %d\n", err,
                getAudioDeviceState());

    // The documented contract is "switches the output device"; a switch that
    // cannot happen must leave the previous, working device in place
    // (miniaudio_changeLinuxBackend_impl() reopens the previous backend).
    CHECK(engineClockRuns(0.1, 1500),
              "engine still playing on %s after the failed JACK switch "
              "(device state %d)",
              backendName(previous), getAudioDeviceState());
    CHECK(getIsValidVoiceHandle(handle) != 0,
          "the voice is still valid after the failed switch");
    // The next play request must not crash either (it reaches the device
    // start path with no device / no context).
    unsigned hash = 0;
    if (loadWaveform(1, false, 1.0f, 1.0f, &hash) == noError)
    {
        unsigned h2 = 0;
        const PlayerErrors perr = play(hash, 0, 0.1f, 0.0f, false, false, 0.0,
                                       0.0, -1, -1, 1.0f, &h2);
        std::printf("  play() after the failed switch -> %d\n", perr);
    }

    std::printf("recover by switching back to %s\n", backendName(previous));
    clearLastBackend();
    const PlayerErrors back = setLinuxAudioBackend(previous);
    CHECK(back == noError, "switch back to %s returned %d",
          backendName(previous), back);
    CHECK(backendMatches(previous, lastBackend()), "reopened '%s'",
          lastBackend().c_str());
    CHECK(engineClockRuns(), "engine clock runs again (device state %d)",
          getAudioDeviceState());
    CHECK(getIsValidVoiceHandle(handle) != 0,
          "the voice survived the failure and the recovery");
}

static void testRapidSwitches(unsigned handle)
{
    std::printf("rapid switches\n");
    int errors = 0;
    long long worstMs = 0;
    for (int i = 0; i < 20; i++)
    {
        const unsigned b = (i % 2) ? kAlsa : kPulse;
        const auto t0 = std::chrono::steady_clock::now();
        if (setLinuxAudioBackend(b) != noError)
            errors++;
        const long long ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        if (ms > worstMs)
            worstMs = ms;
    }
    std::printf("  20 switches, slowest %lldms\n", worstMs);
    CHECK(errors == 0, "%d of 20 switches failed", errors);
    CHECK(worstMs < 3000, "no switch blocked (slowest %lldms)", worstMs);
    CHECK(getIsValidVoiceHandle(handle) != 0, "the voice survived");
    CHECK(engineClockRuns(), "engine clock still runs");
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else if (std::strcmp(argv[i], "--verbose") == 0) gVerbose = true;
    }

    // Start on PulseAudio: the device every switch below goes back to.
    setLinuxAudioBackend(kPulse);
    prepareEngineInit(1);
    const PlayerErrors init = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    if (init != noError || !isInited())
    {
        std::printf("SKIPPED: the engine could not open a PulseAudio device "
                    "(error %d); start a PulseAudio server\n",
                    init);
        return 0;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);
    std::printf("engine up on '%s'\n", lastBackend().c_str());
    if (lastBackend() != "PulseAudio")
    {
        std::printf("SKIPPED: PulseAudio backend not opened ('%s')\n",
                    lastBackend().c_str());
        dispose();
        return 0;
    }
    // ALSA has to work too, or the switch cases are meaningless.
    if (setLinuxAudioBackend(kAlsa) != noError || lastBackend() != "ALSA")
    {
        std::printf("SKIPPED: the ALSA backend cannot open a device here\n");
        dispose();
        return 0;
    }
    setLinuxAudioBackend(kPulse);

    unsigned hash = 0;
    CHECK(loadWaveform(0, false, 1.0f, 1.0f, &hash) == noError, "waveform");
    unsigned handle = 0;
    CHECK(play(hash, 0, 0.2f, 0.0f, false, true, 0.0, 0.0, -1, -1, 1.0f,
               &handle) == noError,
          "play");
    CHECK(engineClockRuns(), "engine clock runs on PulseAudio");

    testSwitchesWhilePlaying(handle);
    setLinuxAudioBackend(kPulse);
    testUnavailableBackend(handle, kPulse);
    testRapidSwitches(handle);

    dispose();
    CHECK(!isInited(), "dispose() after the switches");

    std::printf("%d checks, %d failed, %d known bugs\n", gChecks, gFailures,
                gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}

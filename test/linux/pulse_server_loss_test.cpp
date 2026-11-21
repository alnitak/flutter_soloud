// Native test: the PulseAudio server goes away while the engine plays on
// miniaudio's PulseAudio backend, then comes back.
//
// The script (pulse_server_loss_test.sh) runs a private PulseAudio server
// with a null sink in a temporary runtime directory, points the test at it
// (PULSE_SERVER) and passes the commands that stop and restart it. The test
// links the whole plugin and drives it through the C entry points Dart uses.
//
// Checks:
//   1. While the server is gone, nothing blocks: getAudioDeviceState(),
//      getEngineTime(), play(), stop(), stopAudioDevice(),
//      startAudioDevice(), changeDevice() and setLinuxAudioBackend() each
//      return within a budget (a watchdog aborts the process otherwise).
//   2. The device state stops reporting "started" once the server is gone
//      (the engine notices the loss).
//   3. After the server is back, startAudioDevice() (the Dart
//      SoLoud.startAudioDevice()) brings playback back: the engine clock
//      advances and the voice that was playing is still valid.
//   4. A restart with no call in between: startAudioDevice() resumes
//      playback (the lost stream is stopped, so the start reopens it).
//   5. dispose() returns.
//
//   test/linux/pulse_server_loss_test <stop command> <start command> [--strict] [--verbose]

#include "enums.h"

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
    enum PlayerErrors stop(unsigned int handle);
    int getIsValidVoiceHandle(unsigned int handle);
    double getEngineTime();
    enum AudioDeviceState getAudioDeviceState();
    enum PlayerErrors startAudioDevice();
    enum PlayerErrors stopAudioDevice(unsigned int force);
    enum PlayerErrors changeDevice(int deviceID);
    void setAudioDeviceIdleTimeout(int64_t timeoutMs);
    void setDartEventCallback(void (*voice_ended)(unsigned int *),
                              void (*file_loaded)(enum PlayerErrors *, char *,
                                                  unsigned int *, uint64_t *),
                              void (*state_changed)(enum PlayerStateEvents *),
                              int64_t owner_engine_id);
}

static std::atomic<bool> gVerbose{false};
void soloud_platform_log(const char *fmt, ...)
{
    if (!gVerbose.load())
        return;
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::fflush(stdout);
}

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

static std::atomic<int> gStateEvents[8];
static void onVoiceEnded(unsigned int *h) { std::free(h); }
static void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                         uint64_t *c)
{
    std::free(e);
    std::free(n);
    std::free(h);
    std::free(c);
}
static void onStateChanged(enum PlayerStateEvents *s)
{
    if (s != nullptr && *s >= 0 && *s < 8)
        gStateEvents[*s]++;
    std::free(s);
}

// --- watchdog: a call that blocks for good cannot be caught from inside, so
// abort the process with a clear message instead of hanging the suite.
static std::atomic<long long> gDeadline{0};
static std::atomic<const char *> gCurrentStep{""};
static long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
struct Guarded
{
    explicit Guarded(const char *what, int budgetMs = 5000)
    {
        gCurrentStep = what;
        gDeadline = nowMs() + budgetMs;
    }
    ~Guarded() { gDeadline = 0; }
};
static void watchdog()
{
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const long long d = gDeadline.load();
        if (d != 0 && nowMs() > d)
        {
            std::printf("  FAIL: '%s' blocked past its budget (hang)\n",
                        gCurrentStep.load());
            std::fflush(stdout);
            std::_Exit(3);
        }
    }
}

static bool engineClockRuns(double minSeconds = 0.1, int budgetMs = 3000)
{
    const double start = getEngineTime();
    const long long end = nowMs() + budgetMs;
    while (nowMs() < end)
    {
        if (getEngineTime() - start >= minSeconds)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

static bool waitState(bool (*pred)(AudioDeviceState), int budgetMs)
{
    const long long end = nowMs() + budgetMs;
    while (nowMs() < end)
    {
        if (pred(getAudioDeviceState()))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred(getAudioDeviceState());
}

static int runCommand(const char *cmd)
{
    const int rc = std::system(cmd);
    std::printf("  $ %s -> %d\n", cmd, rc);
    return rc;
}

int main(int argc, char **argv)
{
    const char *stopCmd = nullptr;
    const char *startCmd = nullptr;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else if (std::strcmp(argv[i], "--verbose") == 0) gVerbose = true;
        else if (stopCmd == nullptr) stopCmd = argv[i];
        else startCmd = argv[i];
    }
    if (stopCmd == nullptr || startCmd == nullptr)
    {
        std::printf("usage: pulse_server_loss_test <stop cmd> <start cmd>\n");
        return 2;
    }
    std::thread(watchdog).detach();

    setLinuxAudioBackend(2); // PulseAudio
    prepareEngineInit(1);
    PlayerErrors init;
    {
        Guarded g("initEngine");
        init = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    }
    if (init != noError || !isInited())
    {
        std::printf("SKIPPED: no PulseAudio device (error %d)\n", init);
        return 0;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);

    unsigned hash = 0;
    CHECK(loadWaveform(0, false, 1.0f, 1.0f, &hash) == noError, "waveform");
    unsigned handle = 0;
    CHECK(play(hash, 0, 0.2f, 0.0f, false, true, 0.0, 0.0, -1, -1, 1.0f,
               &handle) == noError,
          "play");
    CHECK(engineClockRuns(), "engine clock runs before the server loss");
    std::printf("playing, device state %d\n", getAudioDeviceState());

    std::printf("stop the PulseAudio server\n");
    runCommand(stopCmd);

    // 2. The engine notices: the state leaves "started" (the stream failed).
    const bool noticed = waitState(
        [](AudioDeviceState s) { return s != audioDeviceStarted; }, 3000);
    std::printf("  device state after the loss: %d, clock %s\n",
                getAudioDeviceState(),
                engineClockRuns(0.1, 800) ? "still advancing" : "stopped");
    CHECK(noticed,
              "the device state still reports started (%d) 3 s after the "
              "PulseAudio server went away",
              getAudioDeviceState());

    // 1. Nothing blocks while the server is gone.
    {
        Guarded g("getAudioDeviceState/getEngineTime");
        (void)getAudioDeviceState();
        (void)getEngineTime();
    }
    unsigned h2 = 0;
    PlayerErrors r;
    {
        Guarded g("play() with the server gone");
        r = play(hash, 0, 0.1f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
                 &h2);
    }
    std::printf("  play() -> %d\n", r);
    {
        Guarded g("stop() with the server gone");
        stop(h2);
    }
    {
        Guarded g("stopAudioDevice() with the server gone");
        r = stopAudioDevice(1);
    }
    std::printf("  stopAudioDevice() -> %d\n", r);
    {
        Guarded g("startAudioDevice() with the server gone", 15000);
        r = startAudioDevice();
    }
    std::printf("  startAudioDevice() -> %d, state %d\n", r,
                getAudioDeviceState());
    CHECK(r != noError, "startAudioDevice() without a server reports an error");
    {
        Guarded g("changeDevice() with the server gone", 15000);
        r = changeDevice(-1);
    }
    std::printf("  changeDevice(-1) -> %d, state %d\n", r,
                getAudioDeviceState());
    // Let the idle scheduler run against the dead device too.
    std::this_thread::sleep_for(std::chrono::milliseconds(800));

    std::printf("restart the PulseAudio server\n");
    CHECK(runCommand(startCmd) == 0, "server restarted");

    // 3. Recovery through the public API.
    {
        Guarded g("startAudioDevice() after the restart", 15000);
        r = startAudioDevice();
    }
    std::printf("  startAudioDevice() -> %d, state %d\n", r,
                getAudioDeviceState());
    bool recovered = r == noError && engineClockRuns();
    if (!recovered)
    {
        // What an app can do next: ask for the default device again.
        {
            Guarded g("changeDevice() after the restart", 15000);
            r = changeDevice(-1);
        }
        std::printf("  changeDevice(-1) -> %d, state %d\n", r,
                    getAudioDeviceState());
        {
            Guarded g("startAudioDevice() after changeDevice", 15000);
            startAudioDevice();
        }
        const bool viaChange = engineClockRuns();
        std::printf("  playback %s after changeDevice()\n",
                    viaChange ? "resumed" : "did not resume");
        if (!viaChange)
        {
            Guarded g("setLinuxAudioBackend(PulseAudio) after the restart",
                      15000);
            r = setLinuxAudioBackend(2);
            std::printf("  setLinuxAudioBackend(PulseAudio) -> %d, playback %s\n",
                        r, engineClockRuns() ? "resumed" : "did not resume");
        }
    }
    CHECK(recovered,
              "startAudioDevice() after the server came back did not resume "
              "playback (state %d)",
              getAudioDeviceState());
    CHECK(getIsValidVoiceHandle(handle) != 0,
          "the voice survived the server loss");

    // 5. A server restart with no API call in between (a user restarting
    // PulseAudio, or a crash and respawn): the old stream is dead, the new
    // server is up. startAudioDevice() is the only "make it play" call an
    // app has.
    std::printf("restart the server again, no calls in between\n");
    CHECK(engineClockRuns(), "playing before the second loss");
    runCommand(stopCmd);
    CHECK(runCommand(startCmd) == 0, "server restarted");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const bool autoResumed = engineClockRuns(0.1, 1500);
    std::printf("  after the restart: state %d, playback %s\n",
                getAudioDeviceState(), autoResumed ? "resumed" : "dead");
    {
        Guarded g("startAudioDevice() after the second restart", 15000);
        r = startAudioDevice();
    }
    const bool resumedByStart = engineClockRuns(0.1, 1500);
    std::printf("  startAudioDevice() -> %d, state %d, playback %s\n", r,
                getAudioDeviceState(), resumedByStart ? "resumed" : "dead");
    CHECK(autoResumed || resumedByStart,
          "after a PulseAudio restart the engine reports state %d but its "
          "clock does not move, and startAudioDevice() returned %d without "
          "reopening the stream",
              getAudioDeviceState(), r);
    if (!autoResumed && !resumedByStart)
    {
        {
            Guarded g("changeDevice() after the second restart", 15000);
            r = changeDevice(-1);
        }
        {
            Guarded g("startAudioDevice() after changeDevice", 15000);
            startAudioDevice();
        }
        const bool viaChange = engineClockRuns();
        std::printf("  changeDevice(-1) -> %d: playback %s\n", r,
                    viaChange ? "resumed (the workaround)" : "still dead");
        CHECK(viaChange, "changeDevice() recovers after a server restart");
    }
    CHECK(getIsValidVoiceHandle(handle) != 0,
          "the voice survived the second loss");

    {
        Guarded g("dispose()", 10000);
        dispose();
    }
    CHECK(!isInited(), "disposed");
    std::printf("state events: started=%d stopped=%d rerouted=%d "
                "start_failed=%d\n",
                gStateEvents[event_started].load(),
                gStateEvents[event_stopped].load(),
                gStateEvents[event_rerouted].load(),
                gStateEvents[event_audio_device_start_failed].load());
    std::printf("%d checks, %d failed, %d known bugs\n", gChecks, gFailures,
                gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}

// The FFI entry points of src/bindings.cpp that platform engine tests drive
// (the same calls the Dart side makes), plus small helpers: a check macro with
// KNOWN BUG support, a recorder for the state events Dart would receive, and
// "is the mixer really running" probes. Header-only; include it from one
// translation unit, built with the engine sources (test/support/engine_sources.sh).

#ifndef SOLOUD_TEST_ENGINE_FFI_H
#define SOLOUD_TEST_ENGINE_FFI_H

#include "enums.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void prepareEngineInit(int64_t owner_engine_id);
enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate, unsigned int bufferSize, unsigned int channels,
                             unsigned int lowLatency, unsigned int devicePeriodFrames,
                             unsigned int renderAheadFrames);
void dispose();
int isInited();
void setDartEventCallback(void (*voice_ended)(unsigned int *),
                          void (*file_loaded)(enum PlayerErrors *, char *, unsigned int *, uint64_t *),
                          void (*state_changed)(enum PlayerStateEvents *), int64_t owner_engine_id);
void setAndroidAAudioAttributes(unsigned int attributes);
enum PlayerErrors loadWaveform(int waveform, bool superWave, float scale, float detune, unsigned int *hash);
enum PlayerErrors play(unsigned int soundHash, unsigned int busId, float volume, float pan, bool paused, bool looping,
                       double loopingStartAt, double loopingEndAt, int loopingStartOffsetAt,
                       int loopingEndOffsetAt, float scale, unsigned int *handle);
enum PlayerErrors setPause(unsigned int handle, bool pause);
enum PlayerErrors stop(unsigned int handle);
void stopAll();
void disposeAllSound();
double getStreamTime(unsigned int handle);
double getEngineTime();
unsigned int getActiveVoiceCount();
int getIsValidVoiceHandle(unsigned int handle);
enum AudioDeviceState getAudioDeviceState();
enum PlayerErrors stopAudioDevice(unsigned int force);
enum PlayerErrors startAudioDevice();
enum PlayerErrors changeDevice(int deviceID);
void setAudioDeviceIdleTimeout(int64_t timeoutMs);
void debugTriggerAudioInterruption(unsigned int began);
void listPlaybackDevicesBounded(char **devicesName, int **deviceId, int **isDefault, int *n_devices, int capacity);
void freeListPlaybackDevices(char **devicesName, int **deviceId, int **isDefault, int n_devices);
}

namespace enginetest {

inline int gFailures = 0;
inline int gKnownBugs = 0;
inline bool gStrict = false;

#define ET_CHECK(cond, ...)                                                     \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  FAIL (line %d): ", __LINE__);                       \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            std::fflush(stdout);                                                \
            ++enginetest::gFailures;                                            \
        } else {                                                                \
            std::printf("  ok: ");                                              \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
        }                                                                       \
    } while (0)

/// A failing check caused by a known src/ bug: printed, counted as a failure
/// only with --strict.
#define ET_KNOWN_BUG(cond, id, ...)                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  KNOWN BUG %s (line %d): ", id, __LINE__);           \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
            std::fflush(stdout);                                                \
            ++enginetest::gKnownBugs;                                           \
            if (enginetest::gStrict) ++enginetest::gFailures;                   \
        } else {                                                                \
            std::printf("  ok (was KNOWN BUG %s): ", id);                      \
            std::printf(__VA_ARGS__);                                           \
            std::printf("\n");                                                  \
        }                                                                       \
    } while (0)

inline void parseArgs(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
}

inline int finish(const char *name) {
    if (gKnownBugs > 0) std::printf("%d known bug check(s) failed (pass --strict to count them).\n", gKnownBugs);
    if (gFailures == 0) {
        std::printf("All %s checks passed.\n", name);
        return 0;
    }
    std::printf("%d %s check(s) failed.\n", gFailures, name);
    return 1;
}

inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

/// The state events Dart would receive (PlayerStateEvents), in order.
struct StateRecorder {
    std::mutex mutex;
    std::vector<int> events;
    std::atomic<int> voiceEnded{0};

    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        events.clear();
        voiceEnded = 0;
    }
    std::vector<int> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return events;
    }
    int count(int event) {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (int e : events) n += e == event;
        return n;
    }
    /// Waits up to [ms] for [event] to have been delivered at least [times].
    bool waitFor(int event, int times = 1, int ms = 3000) {
        for (int waited = 0; waited < ms; waited += 10) {
            if (count(event) >= times) return true;
            sleepMs(10);
        }
        return count(event) >= times;
    }
    std::string describe() {
        static const char *names[] = {"started", "stopped", "rerouted", "interruption_began",
                                      "interruption_ended", "unlocked", "start_failed"};
        std::string out;
        for (int e : snapshot()) {
            if (!out.empty()) out += ", ";
            out += (e >= 0 && e < 7) ? names[e] : std::to_string(e);
        }
        return out.empty() ? "(none)" : out;
    }
};

inline StateRecorder gState;

inline void onStateChanged(enum PlayerStateEvents *e) {
    {
        std::lock_guard<std::mutex> lock(gState.mutex);
        gState.events.push_back(static_cast<int>(*e));
    }
    std::free(e);
}
inline void onVoiceEnded(unsigned int *handle) {
    gState.voiceEnded++;
    std::free(handle);
}
inline void onFileLoaded(enum PlayerErrors *e, char *name, unsigned int *hash, uint64_t *ts) {
    std::free(e);
    std::free(name);
    std::free(hash);
    std::free(ts);
}

/// The Dart init sequence: claim, register the callbacks, open the engine.
inline PlayerErrors initLikeDart(bool lowLatency = false, unsigned int sampleRate = 44100,
                                 unsigned int bufferSize = 2048) {
    prepareEngineInit(-1);
    // Registered before the device opens (Dart registers right after
    // initEngine returns), so the first "started" is recorded too.
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, -1);
    return initEngine(-1, sampleRate, bufferSize, 2, lowLatency ? 1 : 0, 0, 0);
}

/// A looping sine voice at [volume]; returns the handle or 0.
inline unsigned int playSine(float volume = 0.05f, bool paused = false) {
    unsigned int hash = 0;
    if (loadWaveform(0 /* sin */, false, 0.25f, 1.0f, &hash) != noError) return 0;
    unsigned int handle = 0;
    if (play(hash, 0, volume, 0.0f, paused, true, 0.0, 0.0, -1, -1, 1.0f, &handle) != noError) return 0;
    return handle;
}

/// How far [handle]'s stream time advances in [ms] of wall time, in seconds:
/// about ms/1000 while the device pulls audio, 0 when nothing mixes.
inline double streamAdvance(unsigned int handle, int ms = 500) {
    const double before = getStreamTime(handle);
    sleepMs(ms);
    return getStreamTime(handle) - before;
}

/// Whether the mixer runs: [handle] advances by at least half of real time.
inline bool mixing(unsigned int handle, int ms = 500) { return streamAdvance(handle, ms) > ms / 2000.0; }

/// Waits up to [ms] for the device to reach [state].
inline bool waitDeviceState(AudioDeviceState state, int ms = 3000) {
    for (int waited = 0; waited < ms; waited += 10) {
        if (getAudioDeviceState() == state) return true;
        sleepMs(10);
    }
    return getAudioDeviceState() == state;
}

/// The engine's device ID (for initEngine/changeDevice) of the first playback
/// device whose name contains [part], or -2 if there is none.
inline int findEngineDevice(const std::string &part) {
    constexpr int kCap = 64;
    char *names[kCap] = {};
    int *ids[kCap] = {};
    int *defs[kCap] = {};
    int n = 0;
    listPlaybackDevicesBounded(names, ids, defs, &n, kCap);
    int found = -2;
    for (int i = 0; i < n; ++i)
        if (found == -2 && names[i] && std::string(names[i]).find(part) != std::string::npos) found = *ids[i];
    freeListPlaybackDevices(names, ids, defs, n);
    return found;
}

} // namespace enginetest

#endif // SOLOUD_TEST_ENGINE_FFI_H

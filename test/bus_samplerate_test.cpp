// Standalone native tests for the rate a mixing bus is mixed at.
//
// A SoLoud bus mixes its voices at its own rate, the bus's base sample rate,
// which is 44.1 kHz unless set. The engine then resamples the bus's mix to its
// own rate. A scheduled or clocked start is counted in engine samples, but a
// voice inside a bus counts its delay down at the bus's rate, so on an engine
// running at 48 kHz it started about 9% late (a start 4 s ahead was heard
// 0.35 s late). Everything played through the bus was also resampled twice.
//
// What these tests pin down:
//
//   * Player::busPlayOnEngine() plays the bus at the engine's rate;
//   * a sound scheduled into a bus mixed at the engine's rate starts on the
//     engine sample it was scheduled for, where one in a bus at the 44.1 kHz
//     default starts late.
//
// The "player" scenario opens the default output device through initEngine()
// (miniaudio falls back to its null backend without one). The "schedule"
// scenario runs a separate engine on SoLoud's null driver and mixes it by
// hand, so it decides exactly how many samples have been mixed.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_bus_samplerate_test.sh [--asan|--tsan] [scenario]

#include "enums.h"
#include "player.h"

#include "soloud.h"
#include "soloud_bus.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

extern "C"
{
    extern std::unique_ptr<Player> &player;
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency,
                                 unsigned int devicePeriodFrames,
                                 unsigned int renderAheadFrames);
    void dispose();
    unsigned int createBus();
    void destroyBus(unsigned int busId);
    enum PlayerErrors busPlayOnEngine(unsigned int busId, float volume,
                                      bool paused, unsigned int *handle);
}

namespace {

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...) do { \
    ++gAssertions; \
    if (!(condition)) { \
        ++gFailures; \
        std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", \
                     __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

// ---- player -----------------------------------------------------------------

void testBusPlaysAtEngineRate() {
    std::printf("player: busPlayOnEngine plays the bus at the engine's rate\n");

    // Neither is SoLoud's 44.1 kHz default, so a bus left at the default
    // shows up as a mismatch.
    for (unsigned int requested : {48000u, 96000u}) {
        prepareEngineInit(1);
        const PlayerErrors e = initEngine(-1, requested, 2048, 2, 0, 0, 0);
        EXPECT(e == noError, "initEngine(%u) failed: %d", requested, e);
        if (e != noError)
            continue;

        const unsigned int engineRate = player->soloud.getBackendSamplerate();
        EXPECT(engineRate == requested,
               "the engine runs at %u Hz, %u Hz was requested", engineRate,
               requested);

        const unsigned int busId = createBus();
        unsigned int handle = 0;
        const PlayerErrors played = busPlayOnEngine(busId, 1.0f, true, &handle);
        EXPECT(played == noError && handle != 0,
               "busPlayOnEngine failed: %d (handle %u)", played, handle);
        if (played == noError && handle != 0) {
            const float busRate = player->soloud.getSamplerate(handle);
            EXPECT(busRate == static_cast<float>(engineRate),
                   "the bus plays at %g Hz on an engine at %u Hz", busRate,
                   engineRate);
        }

        destroyBus(busId);
        dispose();
    }
}

// ---- schedule ---------------------------------------------------------------

constexpr unsigned int kEngineRate = 48000;
constexpr unsigned int kBlockFrames = 512;
constexpr unsigned int kChannels = 2;

class ConstantInstance : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                          unsigned int aBufferSize) override {
        for (unsigned int c = 0; c < mChannels; c++)
            for (unsigned int i = 0; i < aSamplesToRead; i++)
                aBuffer[c * aBufferSize + i] = 0.5f;
        return aSamplesToRead;
    }
    bool hasEnded() override { return false; }
};

/// Plays 0.5 on every channel, forever, at the engine's rate.
class ConstantSource : public SoLoud::AudioSource {
public:
    ConstantSource() {
        mChannels = kChannels;
        mBaseSamplerate = kEngineRate;
    }
    SoLoud::AudioSourceInstance *createInstance() override {
        return new ConstantInstance();
    }
};

/// Schedules [source] [startSeconds] into the engine's timeline, inside a bus
/// mixed at [busRate], and returns the first output frame it is heard on, or
/// -1 if it is not heard within twice that time.
long long firstAudibleFrame(float busRate, double startSeconds) {
    // Declared before the source and the bus: those stop themselves through
    // the engine when destroyed.
    SoLoud::Soloud soloud;
    if (soloud.init(0, SoLoud::Soloud::NULLDRIVER, kEngineRate, kBlockFrames,
                    kChannels) != SoLoud::SO_NO_ERROR) {
        EXPECT(false, "soloud.init failed");
        return -1;
    }
    ConstantSource source;
    SoLoud::Bus bus;
    bus.mBaseSamplerate = busRate;

    // As Player::playScheduled() schedules a sound into a bus.
    const SoLoud::handle busHandle = soloud.play(bus);
    const SoLoud::handle voice =
        bus.playScheduled(soloud.getEngineTime() + startSeconds, source);
    EXPECT(busHandle != 0 && voice != 0, "could not play (bus %u, voice %u)",
           busHandle, voice);

    std::vector<float> out(kBlockFrames * kChannels);
    const long long limit =
        static_cast<long long>(2.0 * startSeconds * kEngineRate);
    long long found = -1;
    for (long long mixed = 0; found < 0 && mixed < limit; mixed += kBlockFrames) {
        // Interleaved: frame i is out[i * kChannels ...].
        soloud.mix(out.data(), kBlockFrames);
        for (unsigned int i = 0; i < kBlockFrames; i++) {
            if (std::fabs(out[i * kChannels]) > 0.01f) {
                found = mixed + i;
                break;
            }
        }
    }
    soloud.deinit();
    return found;
}

void testScheduledStartInsideBus() {
    std::printf("schedule: a sound scheduled into a bus at the engine's rate "
                "starts on time\n");

    const double startSeconds = 1.0;
    const long long expected =
        static_cast<long long>(startSeconds * kEngineRate);
    // The resampler between the bus and the engine may hold back a few
    // samples; a late start is thousands.
    const long long tolerance = 16;

    const long long atEngineRate =
        firstAudibleFrame(static_cast<float>(kEngineRate), startSeconds);
    EXPECT(atEngineRate >= 0 && std::llabs(atEngineRate - expected) <= tolerance,
           "a bus at %u Hz: heard at frame %lld, scheduled for %lld",
           kEngineRate, atEngineRate, expected);

    // What busPlayOnEngine used to do: the delay runs at 44.1 kHz, so it
    // ends 48000 / 44100 times later.
    const long long atDefaultRate = firstAudibleFrame(44100.0f, startSeconds);
    std::printf("  scheduled for frame %lld: heard at %lld in a bus at %u Hz, "
                "at %lld in one at 44.1 kHz\n",
                expected, atEngineRate, kEngineRate, atDefaultRate);
    EXPECT(atDefaultRate > expected + expected / 20,
           "a bus at 44.1 kHz: heard at frame %lld, expected well after %lld",
           atDefaultRate, expected);
}

} // namespace

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : nullptr;
    auto wanted = [&](const char *name) {
        return only == nullptr || std::strcmp(only, name) == 0;
    };

    if (wanted("player"))
        testBusPlaysAtEngineRate();
    if (wanted("schedule"))
        testScheduledStartInsideBus();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

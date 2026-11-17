// Standalone native regression test for destroying a mixing bus that has
// filters attached while the bus is still playing.
//
// BusData declares `bus` before `filters`, and members are destroyed in
// reverse order. Destroying a playing bus (Player::destroyBus ->
// busMap.erase) therefore used to delete the Filter objects owned by
// `filters` first, and only then let ~AudioSource stop the bus voice. In
// between, the audio thread kept running the bus voice's filter instances,
// which dereference their (now freed) parent filter. In production this
// crashed in AmplitudeModulatorInstance::filter (virtual call on the freed
// mParent) on the CoreAudio IO thread.
//
// The test reproduces that window deterministically: it holds the audio
// mutex (as the mixer does while rendering), destroys the BusData on another
// thread, then runs the bus voice's live filter instance exactly like
// mixBus_internal would. Built with AddressSanitizer, the pre-fix code
// reports a heap-use-after-free here; the fixed code stops the bus before any
// filter is freed, so the destroying thread blocks on the audio mutex instead.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_bus_destroy_filter_test.sh

#include "../src/filters/filters.h"

#include "soloud.h"
#include "soloud_bus.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kBufferSize = 512;
constexpr unsigned int kChannels = 2;

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

void destroyPlayingBusWhileMixing(FilterType type, const char *name) {
    std::printf("destroy playing bus with %s filter while mixing\n", name);

    SoLoud::Soloud soloud;
    if (soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kBufferSize, kChannels) != SoLoud::SO_NO_ERROR) {
        EXPECT(false, "soloud.init failed");
        return;
    }

    auto *busData = new BusData(1, &soloud);
    SoLoud::handle h = soloud.play(busData->bus);
    EXPECT(h != 0, "bus failed to play");
    busData->handle = h;
    EXPECT(busData->filters.addFilter(type) == noError, "addFilter failed");

    // The live instance the audio thread renders for the bus voice.
    SoLoud::FilterInstance *instance =
        busData->bus.mInstance ? busData->bus.mInstance->mFilter[0] : nullptr;
    EXPECT(instance != nullptr, "bus voice has no filter instance");
    if (instance == nullptr) {
        delete busData;
        soloud.deinit();
        return;
    }

    // Hold the audio mutex like the mixer does while rendering a block.
    soloud.lockAudioMutex_internal();

    std::atomic<bool> destroyed{false};
    std::thread destroyer([&] {
        delete busData; // what Player::destroyBus -> busMap.erase does
        destroyed.store(true);
    });

    // Give the destroyer time to free whatever it frees without the lock.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT(!destroyed.load(),
           "bus was destroyed while the audio thread was mid-render");

    // Render a block through the bus voice's filter, as mixBus_internal does.
    // Pre-fix, the parent filter has already been deleted at this point.
    std::vector<float> buffer(kBufferSize * kChannels, 0.5f);
    instance->filter(buffer.data(), kBufferSize, kBufferSize, kChannels,
                     (float)kSampleRate, 0.0);

    soloud.unlockAudioMutex_internal();
    destroyer.join();

    EXPECT(destroyed.load(), "bus destruction did not complete");
    EXPECT(!soloud.isValidVoiceHandle(h), "bus voice still alive");

    soloud.deinit();
}

} // namespace

int main() {
    destroyPlayingBusWhileMixing(AmplitudeModulatorFilter, "amplitude modulator");
    destroyPlayingBusWhileMixing(ParametricEQFilter, "parametric EQ");

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

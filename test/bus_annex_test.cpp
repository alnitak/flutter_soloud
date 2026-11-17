// Standalone native tests for annexing a voice into a mixing bus
// (SoLoud::Bus::annexSound, with a flutter_soloud local patch in
// soloud_bus.cpp) while the bus is not playing.
//
// A bus that is not playing has no voice to route through, so annexing into
// it must leave the voice where it is. It used to:
//
//   * dereference a null engine for a bus that was never played
//     (Bus::mSoloud is only set by Soloud::play);
//   * route the voice to the deleted bus voice's handle once the engine had
//     stopped the bus voice (stop on its handle, stopAll, an outer bus
//     stopping), where nothing mixes it: it kept playing, silently, for good;
//   * or, if nothing had been played through the bus before its voice was
//     stopped, route it to handle 0: the main bus, out of its own bus.
//
// Annexing into a playing bus, paused or not, or into one played again after
// its voice was deleted, still moves the voice.
//
// The engine runs on SoLoud's null driver and is mixed by hand.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_bus_annex_test.sh

#include "soloud.h"
#include "soloud_bus.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kBlockFrames = 512;
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

/// A SoLoud engine on the null driver, mixed by hand. Declare it before any
/// source or bus: those stop themselves through it when destroyed.
struct Engine {
    SoLoud::Soloud soloud;
    bool ok = false;
    std::vector<float> out = std::vector<float>(kBlockFrames * kChannels);

    Engine() {
        ok = soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate,
                         kBlockFrames, kChannels) == SoLoud::SO_NO_ERROR;
    }
    ~Engine() { soloud.deinit(); }

    /// Whether anything is heard in the last of a few mixed blocks.
    bool audible() {
        for (int i = 0; i < 3; i++) soloud.mix(out.data(), kBlockFrames);
        float peak = 0.0f;
        for (float sample : out) peak = std::max(peak, std::fabs(sample));
        return peak > 0.01f;
    }

    /// The handle of the bus [voice] plays through (0 for the main bus), or -1
    /// if it is not playing.
    long long busOf(SoLoud::handle voice) {
        soloud.lockAudioMutex_internal();
        const int ch = soloud.getVoiceFromHandle_internal(voice);
        const long long bus = ch < 0 ? -1 : soloud.mVoice[ch]->mBusHandle;
        soloud.unlockAudioMutex_internal();
        return bus;
    }
};

class ConstantInstance : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                          unsigned int aBufferSize) override {
        for (unsigned int c = 0; c < mChannels; c++) {
            for (unsigned int i = 0; i < aSamplesToRead; i++) {
                aBuffer[c * aBufferSize + i] = 0.5f;
            }
        }
        return aSamplesToRead;
    }
    bool hasEnded() override { return false; }
};

/// Plays 0.5 on every channel, forever.
class ConstantSource : public SoLoud::AudioSource {
public:
    ConstantSource() {
        mChannels = kChannels;
        mBaseSamplerate = kSampleRate;
    }
    SoLoud::AudioSourceInstance *createInstance() override {
        return new ConstantInstance();
    }
};

void testPlayingBus() {
    std::printf("annexing into a playing bus moves the voice into it\n");
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    ConstantSource source;
    SoLoud::Bus bus;

    const SoLoud::handle voice = engine.soloud.play(source);
    EXPECT(engine.audible(), "the voice should be heard on the main bus");

    // Paused, so whatever plays through it is silent. Nothing has been played
    // through it yet, so annexing has to look its handle up.
    engine.soloud.play(bus, 1.0f, 0.0f, /*aPaused=*/true);
    bus.annexSound(voice);

    EXPECT(bus.mChannelHandle != 0 && engine.busOf(voice) == bus.mChannelHandle,
           "the voice should play through the bus (bus %lld, voice on %lld)",
           static_cast<long long>(bus.mChannelHandle), engine.busOf(voice));
    EXPECT(!engine.audible(), "the voice should be silent with the paused bus");
    EXPECT(bus.getActiveVoiceCount() == 1,
           "the bus should count the annexed voice (%u)",
           bus.getActiveVoiceCount());
}

void testBusWhoseVoiceWasStopped() {
    std::printf("annexing into a bus whose voice the engine stopped leaves the "
                "voice where it is\n");
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    ConstantSource source;
    SoLoud::Bus bus;

    const SoLoud::handle busVoice = engine.soloud.play(bus);
    // Playing through the bus makes it look up its handle and keep it.
    bus.play(source);
    engine.soloud.stop(busVoice);

    const SoLoud::handle voice = engine.soloud.play(source);
    bus.annexSound(voice);

    EXPECT(engine.busOf(voice) == 0,
           "the voice should stay on the main bus, not move to the deleted bus "
           "voice's handle (voice on %lld)",
           engine.busOf(voice));
    EXPECT(engine.audible(), "the voice should still be heard");

    // Played again, the bus takes the voice.
    engine.soloud.play(bus, 1.0f, 0.0f, /*aPaused=*/true);
    bus.annexSound(voice);
    EXPECT(bus.mChannelHandle != 0 && engine.busOf(voice) == bus.mChannelHandle,
           "the bus played again should take the voice (bus %lld, voice on "
           "%lld)",
           static_cast<long long>(bus.mChannelHandle), engine.busOf(voice));
    EXPECT(!engine.audible(), "the voice should be silent with the paused bus");
}

void testStoppedBusWithNothingPlayedThrough() {
    std::printf("annexing into a stopped bus that had nothing played through "
                "it leaves the voice on its own bus\n");
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    ConstantSource source;
    SoLoud::Bus home;
    SoLoud::Bus bus;

    // The voice plays through a paused bus, so it is silent.
    engine.soloud.play(home, 1.0f, 0.0f, /*aPaused=*/true);
    const SoLoud::handle voice = home.play(source);
    const SoLoud::handle busVoice = engine.soloud.play(bus);
    engine.soloud.stop(busVoice);

    bus.annexSound(voice);

    EXPECT(engine.busOf(voice) == home.mChannelHandle,
           "the voice should stay on its bus (bus %lld, voice on %lld)",
           static_cast<long long>(home.mChannelHandle), engine.busOf(voice));
    EXPECT(!engine.audible(), "the voice should stay silent with its paused bus");
}

void testNeverPlayedBus() {
    std::printf("annexing into a bus that was never played leaves the voice "
                "where it is\n");
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    ConstantSource source;
    SoLoud::Bus bus;

    const SoLoud::handle voice = engine.soloud.play(source);
    bus.annexSound(voice);

    EXPECT(engine.soloud.isValidVoiceHandle(voice), "the voice should still play");
    EXPECT(engine.busOf(voice) == 0,
           "the voice should stay on the main bus (voice on %lld)",
           engine.busOf(voice));
    EXPECT(engine.audible(), "the voice should still be heard");
}

}  // namespace

int main() {
    testPlayingBus();
    testBusWhoseVoiceWasStopped();
    testStoppedBusWithNothingPlayedThrough();
    // Last: this one used to crash.
    testNeverPlayedBus();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

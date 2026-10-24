// Standalone native tests for per-bus visualization: the audio tap of a SoLoud
// mixing bus (a flutter_soloud local patch in soloud_bus.cpp) feeding an
// Analyzer, whose worker thread hands the analyzed windows to a callback.
//
// What these tests pin down:
//
//   * the tap sees what is mixed into the bus, before the bus's own volume;
//   * nothing is captured -- so no packet is sent -- while nothing is mixed
//     through the bus: the bus voice paused or stopped, an outer bus paused,
//     or no mix running at all (a stopped device);
//   * a playing bus with nothing in it is captured as silence, unlike a paused
//     one, so the two can be told apart;
//   * the engine deleting the bus voice behind Bus's back (stopAll, stop on the
//     bus handle, an outer bus stopping) leaves no dangling instance, stops the
//     bus's own voices, and the bus can play again;
//   * a bus that never had anything played through it does not stop the
//     engine's main-bus voices when it is stopped or replayed, and replaying it
//     does not leave the previous bus voice playing;
//   * channel selection (merged average, all channels, one channel), peaks,
//     invalid settings, and source ids across several buses;
//   * detaching and destroying an analyzer, or a whole BusData, while the
//     audio thread is mixing and the worker is dispatching.
//
// The engine runs on SoLoud's null driver and is mixed by hand, so each test
// decides exactly when audio flows.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_bus_visualization_test.sh

#include "../src/analyzer.h"
#include "../src/filters/filters.h"

#include "soloud.h"
#include "soloud_biquadresonantfilter.h"
#include "soloud_bus.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kBlockFrames = 512;
constexpr unsigned int kChannels = 2;
constexpr int kWindow = 256;

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

bool near(float a, float b, float tolerance = 1e-4f) {
    return std::fabs(a - b) <= tolerance;
}

// ---------------------------------------------------------------------------
// Packets

struct Packet {
    int32_t source;
    int32_t channels;
    int32_t waveSamples;
    int32_t fftSamples;
    std::vector<float> data;

    const float *wave(int channel) const {
        return data.data() + static_cast<size_t>(channel) * waveSamples;
    }
    const float *fft(int channel) const {
        return data.data() + static_cast<size_t>(channels) * waveSamples +
               static_cast<size_t>(channel) * fftSamples;
    }
};

std::mutex gPacketsMutex;
std::vector<Packet> gPackets;

void onPacket(Analyzer &source, const float *data, int32_t channelCount,
              int32_t waveSamples, int32_t fftSamples) {
    Packet packet{source.sourceId(), channelCount, waveSamples, fftSamples,
                  std::vector<float>(data, data + static_cast<size_t>(channelCount) *
                                                      (waveSamples + fftSamples))};
    std::lock_guard<std::mutex> lock(gPacketsMutex);
    gPackets.push_back(std::move(packet));
}

void clearPackets() {
    std::lock_guard<std::mutex> lock(gPacketsMutex);
    gPackets.clear();
}

int packetCount(int32_t source) {
    std::lock_guard<std::mutex> lock(gPacketsMutex);
    int count = 0;
    for (const Packet &packet : gPackets) {
        if (packet.source == source) ++count;
    }
    return count;
}

Packet lastPacket(int32_t source) {
    std::lock_guard<std::mutex> lock(gPacketsMutex);
    for (auto it = gPackets.rbegin(); it != gPackets.rend(); ++it) {
        if (it->source == source) return *it;
    }
    return Packet{-1, 0, 0, 0, {}};
}

// ---------------------------------------------------------------------------
// Engine and sources

/// A SoLoud engine on the null driver, mixed by hand. Declare it before any
/// source or bus: those stop themselves through it when destroyed.
struct Engine {
    SoLoud::Soloud soloud;
    bool ok = false;
    std::vector<float> out = std::vector<float>(kBlockFrames * kChannels);

    Engine() {
        ok = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF,
                         SoLoud::Soloud::NULLDRIVER, kSampleRate, kBlockFrames,
                         kChannels) == SoLoud::SO_NO_ERROR;
    }
    ~Engine() { soloud.deinit(); }

    void mix(int blocks = 1) {
        for (int i = 0; i < blocks; i++) soloud.mix(out.data(), kBlockFrames);
    }

    /// Mix a block at a time until [predicate] holds, giving the worker time
    /// to dispatch in between.
    bool pumpUntil(const std::function<bool()> &predicate, int timeoutMs = 2000) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) return true;
            mix();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        return predicate();
    }

    /// Wait for one new packet of [source], mixing meanwhile.
    bool pumpPacket(int32_t source) {
        const int before = packetCount(source);
        return pumpUntil([&] { return packetCount(source) > before; });
    }

    /// Whether [source] stays silent -- sends nothing -- while the engine keeps
    /// mixing. First lets the worker flush a window captured before the
    /// caller's change, which may still be sent.
    bool sendsNothingWhileMixing(int32_t source) {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        const int before = packetCount(source);
        for (int i = 0; i < 12; i++) {
            mix();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        return packetCount(source) == before;
    }
};

class ConstantSource;

class ConstantInstance : public SoLoud::AudioSourceInstance {
public:
    explicit ConstantInstance(ConstantSource *parent) : mParent(parent) {}
    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                          unsigned int aBufferSize) override;
    bool hasEnded() override { return false; }

private:
    ConstantSource *mParent;
};

/// Plays one constant value per channel, forever.
class ConstantSource : public SoLoud::AudioSource {
public:
    ConstantSource(float left, float right, unsigned int channels = 2) {
        mValues[0] = left;
        mValues[1] = right;
        mChannels = channels;
        mBaseSamplerate = kSampleRate;
    }
    SoLoud::AudioSourceInstance *createInstance() override {
        return new ConstantInstance(this);
    }
    float mValues[2];
};

unsigned int ConstantInstance::getAudio(float *aBuffer,
                                        unsigned int aSamplesToRead,
                                        unsigned int aBufferSize) {
    for (unsigned int c = 0; c < mChannels; c++) {
        for (unsigned int i = 0; i < aSamplesToRead; i++) {
            aBuffer[c * aBufferSize + i] = mParent->mValues[c];
        }
    }
    return aSamplesToRead;
}

/// Play [source] through [bus] with unit gain on both channels, so the bus
/// receives exactly the source's values rather than equal-power panned ones.
SoLoud::handle playUnpanned(Engine &engine, SoLoud::Bus &bus,
                            ConstantSource &source) {
    const SoLoud::handle handle = bus.play(source);
    engine.soloud.setPanAbsolute(handle, 1.0f, 1.0f);
    return handle;
}

/// Every wave sample of [packet]'s [channel] is [value].
bool waveIs(const Packet &packet, int channel, float value) {
    if (packet.waveSamples == 0 || channel >= packet.channels) return false;
    for (int i = 0; i < packet.waveSamples; i++) {
        if (!near(packet.wave(channel)[i], value)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tests

void testTapIsBeforeBusVolume() {
    std::printf("the tap sees what is mixed into the bus, before its volume\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    ConstantSource source(0.5f, 0.5f);
    Analyzer analyzer(1);

    const SoLoud::handle busHandle = engine.soloud.play(bus, 0.25f);
    playUnpanned(engine, bus, source);
    EXPECT(analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                            VISUALIZATION_CHANNEL_MERGED,
                                            kChannels) == noError,
           "enabling should succeed");
    bus.setAudioTap(&analyzer);
    engine.mix(4);
    engine.soloud.setVolume(busHandle, 0.1f);
    engine.mix(4);

    EXPECT(engine.pumpPacket(1), "a packet should arrive");
    const Packet packet = lastPacket(1);
    EXPECT(packet.channels == 1 && packet.waveSamples == kWindow &&
               packet.fftSamples == 0,
           "a merged wave packet should hold one wave window (got %d ch, %d "
           "wave, %d fft)",
           packet.channels, packet.waveSamples, packet.fftSamples);
    EXPECT(waveIs(packet, 0, 0.5f),
           "the wave should be the unscaled 0.5, not scaled by the bus volume "
           "(first sample %f)",
           packet.waveSamples ? packet.wave(0)[0] : -1.0f);
    EXPECT(near(analyzer.getChannelPeak(0), 0.5f) &&
               near(analyzer.getChannelPeak(1), 0.5f),
           "the peaks should be 0.5 (got %f, %f)", analyzer.getChannelPeak(0),
           analyzer.getChannelPeak(1));

    bus.setAudioTap(nullptr);
}

void testNothingIsSentWhileNothingIsMixed() {
    std::printf("nothing is sent while nothing is mixed through the bus\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus outer;
    SoLoud::Bus bus;
    ConstantSource source(0.5f, 0.5f);
    Analyzer analyzer(2);

    const SoLoud::handle outerHandle = engine.soloud.play(outer);
    const SoLoud::handle busHandle = outer.play(bus);
    playUnpanned(engine, bus, source);
    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE_AND_FFT,
                                     VISUALIZATION_CHANNEL_MERGED, kChannels);
    bus.setAudioTap(&analyzer);
    EXPECT(engine.pumpPacket(2), "packets should flow while the bus plays");

    // No mix at all, as with a stopped device.
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    const int idle = packetCount(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(packetCount(2) == idle, "nothing should be sent without a mix");

    engine.soloud.setPause(busHandle, true);
    EXPECT(engine.sendsNothingWhileMixing(2),
           "nothing should be sent while the bus is paused");
    engine.soloud.setPause(busHandle, false);
    EXPECT(engine.pumpPacket(2), "packets should resume once unpaused");

    engine.soloud.setPause(outerHandle, true);
    EXPECT(engine.sendsNothingWhileMixing(2),
           "nothing should be sent while the outer bus is paused");
    engine.soloud.setPause(outerHandle, false);
    EXPECT(engine.pumpPacket(2), "packets should resume once the outer bus is "
                                 "unpaused");

    bus.stop();
    EXPECT(engine.sendsNothingWhileMixing(2),
           "nothing should be sent once the bus is stopped");

    bus.setAudioTap(nullptr);
}

void testEmptyBusIsSilence() {
    std::printf("a playing bus with nothing in it is captured as silence\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    ConstantSource source(0.5f, 0.5f);
    Analyzer analyzer(3);

    engine.soloud.play(bus);
    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                     VISUALIZATION_CHANNEL_MERGED, kChannels);
    bus.setAudioTap(&analyzer);

    // Nothing was ever played through the bus: its handle is not even known.
    EXPECT(engine.pumpPacket(3), "an empty playing bus should send packets");
    EXPECT(waveIs(lastPacket(3), 0, 0.0f), "and they should be silent");

    const SoLoud::handle voice = playUnpanned(engine, bus, source);
    EXPECT(engine.pumpUntil([] { return waveIs(lastPacket(3), 0, 0.5f); }),
           "a sound played through it should show up");

    engine.soloud.setPause(voice, true);
    EXPECT(engine.pumpUntil([] { return waveIs(lastPacket(3), 0, 0.0f); }),
           "with its only sound paused the bus should send silence again");

    bus.setAudioTap(nullptr);
}

void testEngineDeletingTheBusVoice() {
    struct Route {
        const char *name;
        std::function<void(Engine &, SoLoud::Bus &, SoLoud::Bus &,
                           SoLoud::handle)>
            stop;
    };
    const Route routes[] = {
        {"stopAll",
         [](Engine &engine, SoLoud::Bus &, SoLoud::Bus &, SoLoud::handle) {
             engine.soloud.stopAll();
         }},
        {"stop on the bus handle",
         [](Engine &engine, SoLoud::Bus &, SoLoud::Bus &, SoLoud::handle bus) {
             engine.soloud.stop(bus);
         }},
        {"the outer bus stopping",
         [](Engine &, SoLoud::Bus &outer, SoLoud::Bus &, SoLoud::handle) {
             outer.stop();
         }},
    };

    for (const Route &route : routes) {
        std::printf("the engine deleting the bus voice (%s) leaves nothing "
                    "dangling\n",
                    route.name);
        clearPackets();
        Engine engine;
        if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
        SoLoud::Bus outer;
        SoLoud::Bus bus;
        ConstantSource source(0.5f, 0.5f);
        SoLoud::BiquadResonantFilter filter;
        Analyzer analyzer(4);

        engine.soloud.play(outer);
        const SoLoud::handle busHandle = outer.play(bus);
        const SoLoud::handle voice = playUnpanned(engine, bus, source);
        analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                         VISUALIZATION_CHANNEL_MERGED,
                                         kChannels);
        bus.setAudioTap(&analyzer);
        EXPECT(engine.pumpPacket(4), "packets should flow while the bus plays");

        route.stop(engine, outer, bus, busHandle);

        EXPECT(bus.mInstance == nullptr,
               "the bus should no longer point at its deleted instance");
        EXPECT(!engine.soloud.isValidVoiceHandle(voice),
               "the bus's own voice should have been stopped with it");
        EXPECT(bus.play(source) == 0,
               "playing through a bus whose voice is gone should fail rather "
               "than start a voice nothing mixes");
        // Each of these used to dereference the deleted instance.
        bus.setFilter(0, &filter);
        bus.setFilter(0, nullptr);
        bus.calcFFT();
        bus.getWave();
        bus.getApproximateVolume(0);
        EXPECT(engine.sendsNothingWhileMixing(4),
               "nothing should be sent once the bus voice is gone");

        // It plays again, with the same tap.
        if (outer.mInstance == nullptr) {
            engine.soloud.play(outer);
        }
        outer.play(bus);
        playUnpanned(engine, bus, source);
        EXPECT(engine.pumpUntil([] { return waveIs(lastPacket(4), 0, 0.5f); }),
               "the replayed bus should be visualized again");

        bus.setAudioTap(nullptr);
    }
}

void testUnusedBusSparesMainVoices() {
    std::printf("a bus with nothing played through it spares the main voices\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    ConstantSource source(0.5f, 0.5f);

    const SoLoud::handle mainVoice = engine.soloud.play(source);
    engine.soloud.play(bus);
    engine.mix(2);
    bus.stop();
    EXPECT(engine.soloud.isValidVoiceHandle(mainVoice),
           "stopping a bus that never had anything played through it must not "
           "stop the engine's main-bus voices");
    EXPECT(bus.getActiveVoiceCount() == 0,
           "a stopped bus should count no voices, not the main bus's (%u)",
           bus.getActiveVoiceCount());

    engine.soloud.play(bus);
    engine.soloud.play(bus);
    engine.mix(2);
    EXPECT(engine.soloud.countAudioSource(bus) == 1,
           "replaying the bus should stop its previous voice (%d bus voices)",
           engine.soloud.countAudioSource(bus));
    EXPECT(engine.soloud.isValidVoiceHandle(mainVoice),
           "replaying such a bus must not stop the main-bus voices either");
}

void testChannelSelection() {
    std::printf("channel selection, peaks and invalid settings\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    ConstantSource source(0.5f, -0.25f);
    Analyzer analyzer(5);

    engine.soloud.play(bus);
    playUnpanned(engine, bus, source);
    bus.setAudioTap(&analyzer);

    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE_AND_FFT,
                                     VISUALIZATION_CHANNEL_MERGED, kChannels);
    EXPECT(engine.pumpUntil([] { return waveIs(lastPacket(5), 0, 0.125f); }),
           "merged should average the channels to 0.125");
    Packet packet = lastPacket(5);
    EXPECT(packet.channels == 1 && packet.fftSamples == kWindow / 2,
           "merged wave and FFT should hold one channel and windowSize / 2 "
           "bins (got %d ch, %d bins)",
           packet.channels, packet.fftSamples);
    bool inRange = packet.fftSamples > 0;
    for (int i = 0; i < packet.fftSamples; i++) {
        inRange = inRange && packet.fft(0)[i] >= 0.0f && packet.fft(0)[i] <= 1.0f;
    }
    EXPECT(inRange, "FFT magnitudes should be normalized to [0, 1]");
    EXPECT(near(analyzer.getChannelPeak(0), 0.5f) &&
               near(analyzer.getChannelPeak(1), 0.25f),
           "peaks are per channel whatever the selection (got %f, %f)",
           analyzer.getChannelPeak(0), analyzer.getChannelPeak(1));

    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                     VISUALIZATION_CHANNEL_ALL, kChannels);
    EXPECT(engine.pumpUntil([] {
               const Packet p = lastPacket(5);
               return p.channels == 2 && waveIs(p, 0, 0.5f) &&
                      waveIs(p, 1, -0.25f);
           }),
           "all channels should send each channel");

    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE, 1,
                                     kChannels);
    EXPECT(engine.pumpUntil([] {
               const Packet p = lastPacket(5);
               return p.channels == 1 && waveIs(p, 0, -0.25f);
           }),
           "channel 1 should send the right channel only");

    // Invalid settings leave the running visualization alone.
    EXPECT(analyzer.setVisualizationEnabled(true, 100) == invalidParameter,
           "a window that is not a power of two is invalid");
    EXPECT(analyzer.setVisualizationEnabled(true, 16384) == invalidParameter,
           "a window above 8192 is invalid");
    EXPECT(analyzer.setVisualizationEnabled(
               true, kWindow, static_cast<VisualizationKind>(3)) ==
               invalidParameter,
           "an unknown kind is invalid");
    EXPECT(analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                            -3) == invalidParameter,
           "a channel below -2 is invalid");
    EXPECT(analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                            VISUALIZATION_CHANNEL_MERGED,
                                            9) == invalidParameter,
           "more than 8 channels is invalid");
    EXPECT(analyzer.isVisualizationEnabled() && analyzer.channelSelection() == 1,
           "the running visualization should be untouched");

    analyzer.setVisualizationEnabled(false);
    EXPECT(!analyzer.isVisualizationEnabled(), "disabling should stop it");
    EXPECT(analyzer.getChannelPeak(0) == 0.0f,
           "the peaks should read 0 once disabled");
    EXPECT(engine.sendsNothingWhileMixing(5),
           "nothing should be sent once disabled");

    bus.setAudioTap(nullptr);
}

void testMonoBus() {
    std::printf("a mono bus\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    bus.setChannels(1);
    ConstantSource source(0.3f, 0.0f, 1);
    Analyzer analyzer(6);

    engine.soloud.play(bus);
    playUnpanned(engine, bus, source);
    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                     VISUALIZATION_CHANNEL_ALL, 1);
    bus.setAudioTap(&analyzer);
    EXPECT(engine.pumpUntil([] {
               const Packet p = lastPacket(6);
               return p.channels == 1 && waveIs(p, 0, 0.3f);
           }),
           "a mono bus should send its one channel");
    EXPECT(near(analyzer.getChannelPeak(0), 0.3f) &&
               analyzer.getChannelPeak(1) == 0.0f,
           "only channel 0 should have a peak (got %f, %f)",
           analyzer.getChannelPeak(0), analyzer.getChannelPeak(1));

    // Analyzing more channels than the bus mixes pads the rest with silence
    // and keeps the merged average over the channels actually captured.
    analyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                     VISUALIZATION_CHANNEL_MERGED, 2);
    EXPECT(engine.pumpUntil([] { return waveIs(lastPacket(6), 0, 0.3f); }),
           "merged should average only the captured channel");

    bus.setAudioTap(nullptr);
}

void testSourceIdsAcrossBuses() {
    std::printf("several buses keep their own source ids\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus first;
    SoLoud::Bus second;
    ConstantSource quiet(0.1f, 0.1f);
    ConstantSource loud(0.8f, 0.8f);
    Analyzer firstAnalyzer(7);
    Analyzer secondAnalyzer(9);

    engine.soloud.play(first);
    engine.soloud.play(second);
    playUnpanned(engine, first, quiet);
    playUnpanned(engine, second, loud);
    firstAnalyzer.setVisualizationEnabled(true, kWindow, VISUALIZATION_WAVE,
                                          VISUALIZATION_CHANNEL_MERGED,
                                          kChannels);
    secondAnalyzer.setVisualizationEnabled(true, 512, VISUALIZATION_FFT,
                                           VISUALIZATION_CHANNEL_MERGED,
                                           kChannels);
    first.setAudioTap(&firstAnalyzer);
    second.setAudioTap(&secondAnalyzer);

    EXPECT(engine.pumpUntil([] {
               return waveIs(lastPacket(7), 0, 0.1f) &&
                      lastPacket(9).fftSamples == 256;
           }),
           "each bus should send its own packets under its own id");
    EXPECT(lastPacket(9).waveSamples == 0,
           "an FFT-only packet should hold no wave");
    EXPECT(near(firstAnalyzer.getChannelPeak(0), 0.1f) &&
               near(secondAnalyzer.getChannelPeak(0), 0.8f),
           "each analyzer should report its own bus's peak");
    EXPECT(lastPacket(0).source == -1,
           "nothing should be sent under the engine output's id");

    Analyzer::disableAll();
    EXPECT(!firstAnalyzer.isVisualizationEnabled() &&
               !secondAnalyzer.isVisualizationEnabled(),
           "disableAll() should disable every analyzer");
    EXPECT(engine.sendsNothingWhileMixing(7) && engine.sendsNothingWhileMixing(9),
           "nothing should be sent after disableAll()");

    first.setAudioTap(nullptr);
    second.setAudioTap(nullptr);
}

void testTeardownWhileMixing() {
    std::printf("analyzers and buses torn down while mixing and dispatching\n");
    clearPackets();
    Engine engine;
    if (!engine.ok) { EXPECT(false, "soloud.init failed"); return; }
    SoLoud::Bus bus;
    ConstantSource source(0.5f, 0.5f);
    engine.soloud.play(bus);
    playUnpanned(engine, bus, source);

    // The audio thread.
    std::atomic<bool> mixing{true};
    std::thread audio([&] {
        std::vector<float> out(kBlockFrames * kChannels);
        while (mixing.load()) {
            engine.soloud.mix(out.data(), kBlockFrames);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    });

    auto waitForPacket = [](int32_t source) {
        const int before = packetCount(source);
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (packetCount(source) == before &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return packetCount(source) > before;
    };

    int dispatched = 0;
    for (int round = 0; round < 20; round++) {
        auto analyzer = std::make_unique<Analyzer>(10);
        analyzer->setVisualizationEnabled(
            true, kWindow, VISUALIZATION_WAVE_AND_FFT,
            round % 2 ? VISUALIZATION_CHANNEL_ALL : VISUALIZATION_CHANNEL_MERGED,
            kChannels);
        bus.setAudioTap(analyzer.get());
        if (waitForPacket(10)) ++dispatched;
        if (round % 2) {
            // Detach first, as BusData::releaseAnalyzer() does.
            bus.setAudioTap(nullptr);
            analyzer.reset();
        } else {
            // Disable while still attached, as Analyzer::disableAll() does.
            analyzer->setVisualizationEnabled(false);
            bus.setAudioTap(nullptr);
            analyzer.reset();
        }
    }
    EXPECT(dispatched == 20, "every round should have dispatched (%d of 20)",
           dispatched);

    // A whole BusData destroyed while its bus is mixing and visualized.
    for (int round = 0; round < 10; round++) {
        auto busData = std::make_unique<BusData>(11, &engine.soloud);
        engine.soloud.play(busData->bus);
        playUnpanned(engine, busData->bus, source);
        busData->analyzer = std::make_unique<Analyzer>(11);
        busData->analyzer->setVisualizationEnabled(
            true, kWindow, VISUALIZATION_WAVE_AND_FFT,
            VISUALIZATION_CHANNEL_MERGED, kChannels);
        busData->bus.setAudioTap(busData->analyzer.get());
        waitForPacket(11);
        busData.reset();
    }

    mixing = false;
    audio.join();
}

}  // namespace

int main() {
    Analyzer::setDataCallback(onPacket);

    testTapIsBeforeBusVolume();
    testNothingIsSentWhileNothingIsMixed();
    testEmptyBusIsSilence();
    testEngineDeletingTheBusVoice();
    testUnusedBusSparesMainVoices();
    testChannelSelection();
    testMonoBus();
    testSourceIdsAcrossBuses();
    testTeardownWhileMixing();

    Analyzer::setDataCallback(nullptr);
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

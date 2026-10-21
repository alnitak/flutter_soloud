// Standalone native test for mixing bus visualization: Bus::getLevel,
// Bus::getWave and Bus::getApproximateVolume.
//
// Also covers the bus voice being deleted by the engine (stopAll, stop, an
// outer bus stopping). That frees the BusInstance without going through Bus,
// so Bus::mInstance must not be left dangling: built with AddressSanitizer,
// a stale pointer shows up as a heap-use-after-free.
//
// Runs on the null driver and drives the mixer by hand with Soloud::mix, so
// the number of frames mixed through each bus is known exactly.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_bus_visualization_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_bus.h"

#include <cmath>
#include <cstdio>
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

// A stereo source. In sine mode both channels carry the same sine of
// amplitude kAmp. In ramp mode the left channel is n * kStep and the right
// channel is -n * kStep / 2, where n is the frame index, so the channel
// average of frame n is n * kStep / 4.
constexpr float kAmp = 0.5f;
constexpr float kStep = 1.0f / 8192.0f;

class TestSourceInstance final : public SoLoud::AudioSourceInstance {
public:
    bool mRamp = false;
    double mPhase = 0.0;
    double mPhaseStep = 0.0;
    unsigned int mFrame = 0;

    unsigned int getAudio(float *buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int i = 0; i < samplesToRead; ++i) {
            float l, r;
            if (mRamp) {
                l = (float)mFrame * kStep;
                r = -(float)mFrame * kStep * 0.5f;
            } else {
                l = r = (float)std::sin(mPhase) * kAmp;
                mPhase += mPhaseStep;
            }
            buffer[i] = l;
            buffer[i + bufferSize] = r;
            ++mFrame;
        }
        return samplesToRead;
    }

    bool hasEnded() override { return false; }
};

class TestSource final : public SoLoud::AudioSource {
public:
    explicit TestSource(bool aRamp) : mRamp(aRamp) {
        mBaseSamplerate = (float)kSampleRate;
        mChannels = 2;
    }

    SoLoud::AudioSourceInstance *createInstance() override {
        auto *instance = new TestSourceInstance();
        instance->mRamp = mRamp;
        // 441 Hz: exactly 100 frames per period.
        instance->mPhaseStep = 2.0 * M_PI * 441.0 / kSampleRate;
        return instance;
    }

    bool mRamp;
};

struct Level {
    float rms;
    float peak;
    unsigned long long frames;
};

Level readLevel(SoLoud::Bus &bus) {
    Level l;
    bus.getLevel(&l.rms, &l.peak, &l.frames);
    return l;
}

void mixFrames(SoLoud::Soloud &soloud, unsigned int frames) {
    std::vector<float> out(frames * kChannels);
    soloud.mix(out.data(), frames);
}

bool initEngine(SoLoud::Soloud &soloud) {
    const SoLoud::result r =
        soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kBufferSize, kChannels);
    EXPECT(r == SoLoud::SO_NO_ERROR, "soloud.init failed: %d", r);
    return r == SoLoud::SO_NO_ERROR;
}

// Play [source] through [bus] at full, unpanned volume.
SoLoud::handle playThrough(SoLoud::Soloud &soloud, SoLoud::Bus &bus,
                           SoLoud::AudioSource &source) {
    SoLoud::handle h = bus.play(source);
    EXPECT(h != 0, "bus.play failed");
    soloud.setPanAbsolute(h, 1.0f, 1.0f);
    return h;
}

void testSineLevel() {
    std::printf("0.5 sine through a stereo bus\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    bus.setResampler(SoLoud::Soloud::RESAMPLER_POINT);
    SoLoud::handle busHandle = soloud.play(bus);
    EXPECT(busHandle != 0, "bus failed to play");
    bus.setVisualizationEnable(true);

    TestSource sine(false);
    playThrough(soloud, bus, sine);

    // Let the pan ramp settle, then discard what was captured so far.
    mixFrames(soloud, kBufferSize * 2);
    readLevel(bus);

    const unsigned int frames = kBufferSize * 20;
    mixFrames(soloud, frames);
    Level l = readLevel(bus);
    EXPECT(std::fabs(l.rms - kAmp / std::sqrt(2.0f)) < 0.005f,
           "rms %f, expected %f", l.rms, kAmp / std::sqrt(2.0f));
    EXPECT(std::fabs(l.peak - kAmp) < 0.005f, "peak %f, expected %f",
           l.peak, kAmp);
    EXPECT(l.frames == frames, "frames %llu, expected %u", l.frames, frames);

    l = readLevel(bus);
    EXPECT(l.frames == 0, "second read: frames %llu, expected 0", l.frames);
    EXPECT(l.rms == 0 && l.peak == 0, "second read: rms %f peak %f",
           l.rms, l.peak);

    soloud.deinit();
}

void testVolumeDoesNotChangeReading() {
    std::printf("bus volume does not change the reading\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    bus.setResampler(SoLoud::Soloud::RESAMPLER_POINT);
    SoLoud::handle busHandle = soloud.play(bus);
    bus.setVisualizationEnable(true);
    TestSource sine(false);
    playThrough(soloud, bus, sine);

    soloud.setVolume(busHandle, 0.1f);
    mixFrames(soloud, kBufferSize * 2);
    readLevel(bus);

    mixFrames(soloud, kBufferSize * 10);
    Level l = readLevel(bus);
    EXPECT(std::fabs(l.rms - kAmp / std::sqrt(2.0f)) < 0.005f,
           "rms %f with bus volume 0.1", l.rms);
    EXPECT(std::fabs(l.peak - kAmp) < 0.005f, "peak %f with bus volume 0.1",
           l.peak);

    soloud.deinit();
}

void testPausedBus() {
    std::printf("paused bus and bus inside a paused bus\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus outer;
    SoLoud::Bus inner;
    SoLoud::handle outerHandle = soloud.play(outer);
    SoLoud::handle innerHandle = outer.play(inner);
    EXPECT(outerHandle != 0 && innerHandle != 0, "busses failed to play");
    soloud.setPanAbsolute(innerHandle, 1.0f, 1.0f);
    outer.setVisualizationEnable(true);
    inner.setVisualizationEnable(true);

    TestSource sine(false);
    playThrough(soloud, inner, sine);

    mixFrames(soloud, kBufferSize * 4);
    EXPECT(readLevel(outer).frames > 0, "outer bus mixed nothing");
    EXPECT(readLevel(inner).frames > 0, "inner bus mixed nothing");

    // Pausing the inner bus only.
    soloud.setPause(innerHandle, true);
    mixFrames(soloud, kBufferSize * 4);
    Level l = readLevel(inner);
    EXPECT(l.frames == 0, "paused bus: frames %llu, expected 0", l.frames);
    soloud.setPause(innerHandle, false);
    EXPECT(readLevel(outer).frames > 0, "outer bus stopped with inner paused");

    // Pausing the outer bus stops the inner one from being mixed too.
    soloud.setPause(outerHandle, true);
    mixFrames(soloud, kBufferSize * 4);
    l = readLevel(outer);
    EXPECT(l.frames == 0, "paused outer bus: frames %llu, expected 0", l.frames);
    l = readLevel(inner);
    EXPECT(l.frames == 0, "bus in paused outer bus: frames %llu, expected 0",
           l.frames);

    soloud.deinit();
}

void testWaveOrderAndAverage() {
    std::printf("getWave returns the latest frames in order, averaged\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    bus.setResampler(SoLoud::Soloud::RESAMPLER_POINT);
    soloud.play(bus);
    bus.setVisualizationEnable(true);
    TestSource ramp(true);
    playThrough(soloud, bus, ramp);

    mixFrames(soloud, kBufferSize * 2);

    // The mixer feeds the bus in whole blocks, which keeps the ring aligned.
    // Pull odd-sized blocks straight from the bus instance so the ring's
    // write position lands mid-buffer and getWave has to unroll it.
    std::vector<float> scratch(kBufferSize * kChannels);
    soloud.lockAudioMutex_internal();
    for (int i = 0; i < 5; ++i)
        bus.mInstance->getAudio(scratch.data(), 100, kBufferSize);
    const unsigned int pos = bus.mInstance->mVisualizationWavePos;
    soloud.unlockAudioMutex_internal();
    EXPECT(pos != 0, "ring position is 0, unrolling is not exercised");

    // The last frame pulled is the last value of the final block.
    const float last = scratch[99] * 0.5f + scratch[99 + kBufferSize] * 0.5f;

    const float *wave = bus.getWave();
    const float expectedStep = kStep / 4.0f;
    bool ordered = true;
    for (int i = 1; i < 256; ++i) {
        if (std::fabs((wave[i] - wave[i - 1]) - expectedStep) > 1e-6f) {
            ordered = false;
            std::fprintf(stderr, "  wave[%d]=%f wave[%d]=%f\n", i - 1,
                         wave[i - 1], i, wave[i]);
            break;
        }
    }
    EXPECT(ordered, "wave is not the averaged ramp in order");
    EXPECT(std::fabs(wave[255] - last) < 1e-6f, "wave[255] %f, expected %f",
           wave[255], last);
    EXPECT(last > 0, "ramp did not reach the bus");

    soloud.deinit();
}

void testEnablingClearsData() {
    std::printf("enabling clears earlier data\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    soloud.play(bus);
    bus.setVisualizationEnable(true);
    TestSource sine(false);
    playThrough(soloud, bus, sine);
    mixFrames(soloud, kBufferSize * 4);

    bus.setVisualizationEnable(false);
    bus.setVisualizationEnable(true);
    Level l = readLevel(bus);
    EXPECT(l.frames == 0, "frames %llu after re-enabling", l.frames);
    const float *wave = bus.getWave();
    bool zero = true;
    for (int i = 0; i < 256; ++i)
        zero = zero && wave[i] == 0;
    EXPECT(zero, "wave not cleared after re-enabling");

    // Nothing is captured while disabled.
    bus.setVisualizationEnable(false);
    mixFrames(soloud, kBufferSize * 4);
    l = readLevel(bus);
    EXPECT(l.frames == 0, "frames %llu while disabled", l.frames);

    soloud.deinit();
}

void testApproximateVolumeBounds() {
    std::printf("getApproximateVolume(channels) returns 0\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    soloud.play(bus);
    bus.setVisualizationEnable(true);
    TestSource sine(false);
    playThrough(soloud, bus, sine);
    mixFrames(soloud, kBufferSize * 4);

    EXPECT(bus.getApproximateVolume(0) > 0.4f, "channel 0 volume %f",
           bus.getApproximateVolume(0));
    EXPECT(bus.getApproximateVolume(1) > 0.4f, "channel 1 volume %f",
           bus.getApproximateVolume(1));
    EXPECT(bus.getApproximateVolume(kChannels) == 0,
           "channel %u volume %f", kChannels,
           bus.getApproximateVolume(kChannels));

    soloud.deinit();
}

// After the bus voice was deleted by the engine, every Bus entry point that
// touches the instance must see it gone, and replaying the bus must work.
void expectInstanceGone(SoLoud::Bus &bus, const char *how) {
    EXPECT(bus.mInstance == nullptr, "%s: mInstance left dangling", how);

    // Each of these used to read (and getLevel / setVisualizationEnable to
    // write) the freed BusInstance.
    Level l = readLevel(bus);
    EXPECT(l.frames == 0, "%s: frames %llu, expected 0", how, l.frames);
    bus.getWave();
    bus.calcFFT();
    EXPECT(bus.getApproximateVolume(0) == 0, "%s: channel 0 volume %f", how,
           bus.getApproximateVolume(0));
    bus.setVisualizationEnable(false);
    bus.setVisualizationEnable(true);
    bus.setFilter(0, nullptr);
    bus.moveFilter(0, 1);
    TestSource sine(false);
    EXPECT(bus.play(sine) == 0,
           "%s: play on a stopped bus returned a handle", how);
}

void replayAndMeasure(SoLoud::Soloud &soloud, SoLoud::Bus &bus,
                      TestSource &sine, const char *how) {
    SoLoud::handle h = soloud.play(bus);
    EXPECT(h != 0, "%s: bus failed to replay", how);
    EXPECT(bus.mInstance != nullptr, "%s: no instance after replay", how);
    playThrough(soloud, bus, sine);
    mixFrames(soloud, kBufferSize * 2);
    readLevel(bus);
    mixFrames(soloud, kBufferSize * 4);
    Level l = readLevel(bus);
    EXPECT(l.frames == kBufferSize * 4, "%s: frames %llu after replay", how,
           l.frames);
    EXPECT(std::fabs(l.peak - kAmp) < 0.005f, "%s: peak %f after replay", how,
           l.peak);
}

void testStoppedBusVoice(int how) {
    const char *names[] = {"stopAll", "bus.stop", "stopped outer bus"};
    const char *name = names[how];
    std::printf("bus voice deleted by %s\n", name);
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus outer;
    SoLoud::Bus bus;
    bus.setResampler(SoLoud::Soloud::RESAMPLER_POINT);
    SoLoud::handle outerHandle = 0;
    if (how == 2) {
        outerHandle = soloud.play(outer);
        SoLoud::handle h = outer.play(bus);
        EXPECT(h != 0, "inner bus failed to play");
        soloud.setPanAbsolute(h, 1.0f, 1.0f);
    } else {
        EXPECT(soloud.play(bus) != 0, "bus failed to play");
    }
    bus.setVisualizationEnable(true);
    TestSource sine(false);
    playThrough(soloud, bus, sine);
    mixFrames(soloud, kBufferSize * 2);
    EXPECT(readLevel(bus).frames > 0, "%s: bus mixed nothing", name);

    if (how == 0)
        soloud.stopAll();
    else if (how == 1)
        bus.stop();
    else
        soloud.stop(outerHandle); // ~BusInstance of outer stops the inner bus

    expectInstanceGone(bus, name);
    mixFrames(soloud, kBufferSize * 2);
    expectInstanceGone(bus, name);
    replayAndMeasure(soloud, bus, sine, name);

    soloud.deinit();
}

// Replaying a bus deletes the old instance through the engine while the new
// one is already in place. The old destructor must not clear the new
// pointer.
void testReplayKeepsNewInstance() {
    std::printf("replaying a bus keeps the new instance\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    bus.setResampler(SoLoud::Soloud::RESAMPLER_POINT);
    soloud.play(bus);
    bus.setVisualizationEnable(true);
    TestSource sine(false);
    replayAndMeasure(soloud, bus, sine, "replay");

    soloud.deinit();
}

// ~BusInstance stops the voices playing on the bus by matching
// mChannelHandle, which is 0 (the main bus) until something is played
// through the bus. Stopping such a bus must leave main bus voices alone.
void testStopUnusedBusKeepsMainVoices() {
    std::printf("stopping an unused bus keeps main bus voices\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    TestSource sine(false);
    SoLoud::handle mainVoice = soloud.play(sine);
    SoLoud::Bus bus;
    SoLoud::handle busHandle = soloud.play(bus);
    EXPECT(busHandle != 0, "bus failed to play");
    soloud.stop(busHandle);
    EXPECT(soloud.isValidVoiceHandle(mainVoice),
           "stopping the bus stopped a main bus voice");
    EXPECT(bus.mInstance == nullptr, "mInstance left dangling");

    soloud.deinit();
}

// Replaying a bus that never had anything played through it must still stop
// the previous bus voice instead of leaving it orphaned.
void testReplayUnusedBus() {
    std::printf("replaying an unused bus stops the old voice\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    SoLoud::Bus bus;
    SoLoud::handle first = soloud.play(bus);
    SoLoud::handle second = soloud.play(bus);
    EXPECT(second != 0, "bus failed to replay");
    EXPECT(!soloud.isValidVoiceHandle(first), "old bus voice left playing");
    EXPECT(soloud.getActiveVoiceCount() == 1, "%u active voices, expected 1",
           soloud.getActiveVoiceCount());

    soloud.deinit();
}

// With every voice taken and protected, play(bus) fails and deletes the new
// BusInstance. That must not stop main bus voices or leave mInstance set.
void testFailedBusPlay() {
    std::printf("failed bus play\n");
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;

    TestSource sine(false);
    std::vector<SoLoud::handle> voices;
    for (int i = 0; i < VOICE_COUNT; i++) {
        SoLoud::handle h = soloud.play(sine, 1.0f, 0.0f, true);
        soloud.setProtectVoice(h, true);
        voices.push_back(h);
    }
    SoLoud::Bus bus;
    EXPECT(soloud.play(bus) == 0, "bus played with no free voice");
    EXPECT(bus.mInstance == nullptr, "mInstance left set after failed play");
    int alive = 0;
    for (SoLoud::handle h : voices)
        alive += soloud.isValidVoiceHandle(h) ? 1 : 0;
    EXPECT(alive == VOICE_COUNT, "failed bus play stopped %d voices",
           VOICE_COUNT - alive);

    soloud.deinit();
}

} // namespace

int main() {
    testSineLevel();
    testVolumeDoesNotChangeReading();
    testPausedBus();
    testWaveOrderAndAverage();
    testEnablingClearsData();
    testApproximateVolumeBounds();
    testStoppedBusVoice(0);
    testStoppedBusVoice(1);
    testStoppedBusVoice(2);
    testReplayKeepsNewInstance();
    testStopUnusedBusKeepsMainVoices();
    testReplayUnusedBus();
    testFailedBusPlay();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

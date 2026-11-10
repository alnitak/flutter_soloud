// Standalone native tests for filter instances and the core filter
// operations, on the null backend (no audio device needed).
//
// Scenarios (pass one name to run only that one):
//
//   getter  getFilterParameter() on a voice handle races voices that end in
//           the mix and new voices that take their slots. The handle must be
//           resolved under the audio mutex: resolving it first reads the
//           voice table (and the voice's play index) while the mixer can
//           delete that voice. Run under ThreadSanitizer (--tsan), which
//           reports the unsynchronized voice-table read.
//
//   compressor  A new compressor instance (each voice that plays with the
//           filter gets one) fed a steady signal well below its threshold
//           must leave it untouched from the first sample: its level
//           detector has to start from silence. One that starts at full
//           scale ducks the start of every voice until it releases.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_filter_ops_test.sh [--asan|--tsan] [scenario]

#include "filters/compressor.h"

#include "soloud.h"
#include "soloud_biquadresonantfilter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kBuffer = 512;
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

// A stereo tone. With a frame limit the voice ends after that many frames,
// which the mixer notices and stops (deleting the voice instance) inside
// the mix block.
class ToneSource;

class ToneInstance final : public SoLoud::AudioSourceInstance {
public:
    explicit ToneInstance(ToneSource *parent) : parent_(parent) {}
    unsigned int getAudio(float *buffer, unsigned int samples,
                          unsigned int bufferSize) override;
    bool hasEnded() override;

private:
    ToneSource *parent_;
    unsigned int offset_ = 0;
};

class ToneSource final : public SoLoud::AudioSource {
public:
    explicit ToneSource(unsigned int frames = 0) : frames(frames) {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = kChannels;
    }
    SoLoud::AudioSourceInstance *createInstance() override {
        return new ToneInstance(this);
    }
    const unsigned int frames;
};

unsigned int ToneInstance::getAudio(float *buffer, unsigned int samples,
                                    unsigned int bufferSize) {
    unsigned int count = samples;
    if (parent_->frames != 0)
        count = std::min(samples, parent_->frames - std::min(offset_, parent_->frames));
    for (unsigned int ch = 0; ch < mChannels; ++ch)
        for (unsigned int i = 0; i < count; ++i)
            buffer[ch * bufferSize + i] =
                0.25f * std::sin(0.05f * static_cast<float>(offset_ + i) +
                                 static_cast<float>(ch));
    offset_ += count;
    return count;
}

bool ToneInstance::hasEnded() {
    return parent_->frames != 0 && offset_ >= parent_->frames;
}

bool initEngine(SoLoud::Soloud &soloud) {
    const SoLoud::result r = soloud.init(SoLoud::Soloud::CLIP_ROUNDOFF,
                                         SoLoud::Soloud::NULLDRIVER,
                                         kSampleRate, kBuffer, kChannels);
    EXPECT(r == SoLoud::SO_NO_ERROR, "soloud.init failed: %u", r);
    return r == SoLoud::SO_NO_ERROR;
}

// ---- getter -----------------------------------------------------------------

void testFilterGetterRacesVoiceEnd() {
    std::printf("getter: getFilterParameter racing voices that end in the mix\n");

    SoLoud::Soloud soloud;
    if (!initEngine(soloud))
        return;

    // Shorter than one block: every voice ends, and is deleted, in the first
    // mix block that renders it.
    ToneSource blip(64);
    SoLoud::BiquadResonantFilter biquad;
    blip.setFilter(0, &biquad);

    std::atomic<bool> stop{false};
    std::atomic<SoLoud::handle> latest{0};
    std::atomic<unsigned long> reads{0};
    std::atomic<unsigned long> plays{0};

    std::thread mixer([&] {
        std::vector<float> out(256 * kChannels);
        while (!stop.load())
            soloud.mix(out.data(), 256);
    });
    std::thread player([&] {
        while (!stop.load()) {
            const SoLoud::handle h = soloud.play(blip);
            if (h != 0) {
                latest.store(h);
                plays.fetch_add(1);
            }
            std::this_thread::yield();
        }
    });
    std::thread getter([&] {
        while (!stop.load()) {
            const SoLoud::handle h = latest.load();
            if (h != 0) {
                (void)soloud.getFilterParameter(h, 0, 0);
                reads.fetch_add(1);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    stop.store(true);
    getter.join();
    player.join();
    mixer.join();

    std::printf("  %lu plays, %lu filter reads\n", plays.load(), reads.load());
    EXPECT(plays.load() > 100, "too few voices were played to race: %lu",
           plays.load());
    EXPECT(reads.load() > 100, "too few filter reads: %lu", reads.load());

    soloud.stopAll();
    std::vector<float> out(kBuffer * kChannels);
    soloud.mix(out.data(), kBuffer);
    blip.setFilter(0, nullptr);
    soloud.deinit();
}

// ---- compressor -------------------------------------------------------------

void testCompressorStartsFromSilence() {
    std::printf("compressor: a signal below the threshold passes untouched from the start\n");
    constexpr unsigned int kFrames = 512;
    constexpr float kLevel = 0.1f; // -20 dBFS, below the default -6 dB threshold

    Compressor parent(kSampleRate);
    std::unique_ptr<SoLoud::FilterInstance> instance(parent.createInstance());

    std::vector<float> buffer(kFrames * kChannels);
    int changed = 0;
    float firstGain = 1.0f;
    float minGain = 1.0f;
    for (int block = 0; block < 16; ++block) {
        for (unsigned int ch = 0; ch < kChannels; ++ch)
            for (unsigned int i = 0; i < kFrames; ++i)
                buffer[ch * kFrames + i] = (i % 2 == 0) ? kLevel : -kLevel;
        instance->filter(buffer.data(), kFrames, kFrames, kChannels,
                         static_cast<float>(kSampleRate), 0.0);
        if (block == 0)
            firstGain = std::fabs(buffer[0]) / kLevel;
        for (float s : buffer) {
            minGain = std::min(minGain, std::fabs(s) / kLevel);
            if (std::fabs(std::fabs(s) - kLevel) > 1e-4f)
                ++changed;
        }
    }
    EXPECT(changed == 0,
           "%d of %u samples changed; the first came out at %.2f dB, the lowest at "
           "%.2f dB",
           changed, 16 * kFrames * kChannels, 20.0f * std::log10(firstGain),
           20.0f * std::log10(minGain));

    // A signal above the threshold is still compressed, once the attack has
    // caught up with it.
    for (unsigned int ch = 0; ch < kChannels; ++ch)
        for (unsigned int i = 0; i < kFrames; ++i)
            buffer[ch * kFrames + i] = (i % 2 == 0) ? 0.9f : -0.9f;
    for (int block = 0; block < 8; ++block)
        instance->filter(buffer.data(), kFrames, kFrames, kChannels,
                         static_cast<float>(kSampleRate), 0.0);
    const float loudGain = std::fabs(buffer[kFrames - 1]) / 0.9f;
    EXPECT(loudGain < 0.9f, "a signal 5 dB over the threshold is not compressed "
           "(gain %.3f)", loudGain);
}

} // namespace

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : nullptr;
    auto wanted = [&](const char *name) {
        return only == nullptr || std::strcmp(only, name) == 0;
    };

    if (wanted("getter"))
        testFilterGetterRacesVoiceEnd();
    if (wanted("compressor"))
        testCompressorStartsFromSilence();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

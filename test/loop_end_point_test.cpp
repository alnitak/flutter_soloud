// Standalone correctness tests for native loop regions.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_loop_end_point_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 1024;

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

double secondsForFrame(unsigned int frame,
                       unsigned int sampleRate = kSampleRate) {
    return static_cast<double>(frame) / sampleRate;
}

class TrackingSource;

class TrackingSourceInstance final : public SoLoud::AudioSourceInstance {
public:
    explicit TrackingSourceInstance(TrackingSource* parent);

    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override;
    bool hasEnded() override;
    SoLoud::result seek(SoLoud::time seconds, float* scratch,
                        unsigned int scratchSize) override;
    SoLoud::result rewind() override;

private:
    TrackingSource* parent_;
    unsigned int offset_ = 0;
};

class TrackingSource final : public SoLoud::AudioSource {
public:
    explicit TrackingSource(unsigned int frameCount,
                            unsigned int sampleRate = kSampleRate)
        : frameCount(frameCount) {
        mBaseSamplerate = static_cast<float>(sampleRate);
        mChannels = 1;
    }

    SoLoud::AudioSourceInstance* createInstance() override {
        return new TrackingSourceInstance(this);
    }

    unsigned int frameCount;
    std::vector<unsigned int> framesRead;
    unsigned int seekCount = 0;
};

TrackingSourceInstance::TrackingSourceInstance(TrackingSource* parent)
    : parent_(parent) {
}

unsigned int TrackingSourceInstance::getAudio(float* buffer,
                                               unsigned int samplesToRead,
                                               unsigned int bufferSize) {
    const unsigned int available = parent_->frameCount -
        std::min(offset_, parent_->frameCount);
    const unsigned int count = std::min(samplesToRead, available);
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int frame = offset_ + i;
        buffer[i] = static_cast<float>((frame % 100) + 1) / 100.0f;
        parent_->framesRead.push_back(frame);
    }
    for (unsigned int channel = 1; channel < mChannels; ++channel) {
        std::fill(buffer + channel * bufferSize,
                  buffer + channel * bufferSize + count, 0.0f);
    }
    offset_ += count;
    return count;
}

bool TrackingSourceInstance::hasEnded() {
    return offset_ >= parent_->frameCount;
}

SoLoud::result TrackingSourceInstance::seek(SoLoud::time seconds,
                                             float* /*scratch*/,
                                             unsigned int /*scratchSize*/) {
    const auto requestedFrame = static_cast<unsigned int>(
        std::floor(seconds * mBaseSamplerate));
    offset_ = std::min(requestedFrame, parent_->frameCount);
    mStreamPosition = static_cast<float>(offset_) / mBaseSamplerate;
    ++parent_->seekCount;
    return SoLoud::SO_NO_ERROR;
}

SoLoud::result TrackingSourceInstance::rewind() {
    offset_ = 0;
    mStreamPosition = 0;
    return SoLoud::SO_NO_ERROR;
}

struct LoopRig {
    explicit LoopRig(unsigned int sourceFrames,
                     unsigned int sampleRate = kSampleRate)
        : sampleRate(sampleRate), source(sourceFrames, sampleRate) {
        const SoLoud::result result = engine.init(
            SoLoud::Soloud::CLIP_ROUNDOFF,
            SoLoud::Soloud::NULLDRIVER,
            sampleRate,
            512,
            1);
        EXPECT(result == SoLoud::SO_NO_ERROR,
               "null backend initialization failed: %d", result);
        engine.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
    }

    ~LoopRig() {
        engine.deinit();
    }

    SoLoud::handle start(unsigned int loopStartFrame,
                         unsigned int loopEndFrame,
                         bool inaudibleTick = false) {
        const SoLoud::handle handle = inaudibleTick
            ? engine.play3d(source, 0.0f, 0.0f, 0.0f,
                            0.0f, 0.0f, 0.0f, 0.0f, true)
            : engine.play(source, 1.0f, 0.0f, true);
        EXPECT(handle != 0, "play returned an invalid handle");
        engine.setLoopPoint(
            handle, secondsForFrame(loopStartFrame, sampleRate));
        engine.setLoopEndPoint(
            handle, secondsForFrame(loopEndFrame, sampleRate));
        engine.setLooping(handle, true);
        if (inaudibleTick) {
            engine.setInaudibleBehavior(handle, true, false);
        }
        engine.setPause(handle, false);
        return handle;
    }

    void mix(unsigned int frameCount = 1024) {
        std::vector<float> output(frameCount, 0.0f);
        engine.mix(output.data(), frameCount);
    }

    unsigned int sampleRate;
    TrackingSource source;
    SoLoud::Soloud engine;
};

void expectLoopPattern(const std::vector<unsigned int>& actual,
                       unsigned int firstFrame,
                       unsigned int firstEnd,
                       unsigned int loopStart,
                       unsigned int loopEnd,
                       unsigned int count) {
    EXPECT(actual.size() >= count,
           "expected at least %u decoded frames, got %zu", count,
           actual.size());
    const unsigned int checked = std::min<unsigned int>(count, actual.size());
    for (unsigned int i = 0; i < checked; ++i) {
        unsigned int expected;
        if (i < firstEnd - firstFrame) {
            expected = firstFrame + i;
        } else {
            expected = loopStart +
                ((i - (firstEnd - firstFrame)) % (loopEnd - loopStart));
        }
        EXPECT(actual[i] == expected,
               "decoded frame %u was %u, expected %u", i, actual[i], expected);
    }
}

void testShortLoopDoesNotCrossEndPoint() {
    std::printf("Test: short loop stays inside [start, end)\n");
    LoopRig rig(64);
    rig.start(5, 8);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 8, 5, 8, 80);
}

void testExactRefillBoundaryLoopsBeforeNextFrame() {
    std::printf("Test: exact 512-frame boundary loops before frame 512\n");
    LoopRig rig(800);
    rig.start(500, 512);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 512, 500, 512, 540);
}

void testEndBeyondSourceFallsBackToNaturalEof() {
    std::printf("Test: end beyond source falls back to natural EOF\n");
    LoopRig rig(10);
    rig.start(5, 20);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 10, 5, 10, 60);
}

void testClearingEndPointRestoresNaturalEof() {
    std::printf("Test: clearing endpoint restores natural EOF\n");
    LoopRig rig(10);
    const SoLoud::handle handle = rig.start(5, 8);
    rig.engine.setPause(handle, true);
    rig.engine.setLoopEndPoint(handle, 0);
    EXPECT(rig.engine.getLoopEndPoint(handle) == 0,
           "cleared endpoint should read back as zero");
    rig.engine.setPause(handle, false);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 10, 5, 10, 60);
}

void testInvalidNativeRangeIsSafelyCleared() {
    std::printf("Test: invalid native endpoint is safely cleared\n");
    LoopRig rig(10);
    const SoLoud::handle handle = rig.start(5, 4);
    EXPECT(rig.engine.getLoopEndPoint(handle) == 0,
           "endpoint at or before start should be cleared");
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 10, 5, 10, 60);
}

void testMovingStartPastEndClearsEndPoint() {
    std::printf("Test: moving start past endpoint clears endpoint\n");
    LoopRig rig(10);
    const SoLoud::handle handle = rig.engine.play(
        rig.source, 1.0f, 0.0f, true);
    rig.engine.setLoopEndPoint(handle, secondsForFrame(4));
    rig.engine.setLoopPoint(handle, secondsForFrame(5));

    EXPECT(rig.engine.getLoopEndPoint(handle) == 0,
           "moving start to or beyond endpoint should clear endpoint");
}

void testPublicSeekSynchronizesLoopCursor() {
    std::printf("Test: public seek synchronizes the source-frame cursor\n");
    LoopRig rig(64);
    const SoLoud::handle handle = rig.start(5, 8);
    rig.engine.setPause(handle, true);
    EXPECT(rig.engine.seek(handle, secondsForFrame(6)) == SoLoud::SO_NO_ERROR,
           "seek to the loop region failed");
    rig.engine.setPause(handle, false);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 6, 8, 5, 8, 60);
}

void testCommonSampleRateDoesNotCrossEndPoint() {
    std::printf("Test: 44.1 kHz loop does not cross the exclusive end\n");
    LoopRig rig(80, 44100);
    rig.start(44, 47);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 47, 44, 47, 80);
}

void testCommonSampleRateSeekSynchronizesLoopCursor() {
    std::printf("Test: 44.1 kHz public seek keeps an exact source cursor\n");
    LoopRig rig(80, 44100);
    const SoLoud::handle handle = rig.start(44, 47);
    rig.engine.setPause(handle, true);
    EXPECT(rig.engine.seek(handle, secondsForFrame(45, 44100)) ==
               SoLoud::SO_NO_ERROR,
           "seek inside the 44.1 kHz loop failed");
    rig.engine.setPause(handle, false);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 45, 47, 44, 47, 80);
}

void testLiveEndPointChangeAppliesAtNextRefill() {
    std::printf("Test: live endpoint change waits for the next refill\n");
    LoopRig rig(64);
    const SoLoud::handle handle = rig.start(5, 10);

    rig.mix(1);
    EXPECT(rig.source.framesRead.size() == 512,
           "first output frame should prefetch 512 source frames, got %zu",
           rig.source.framesRead.size());
    expectLoopPattern(rig.source.framesRead, 0, 10, 5, 10, 512);

    rig.engine.setLoopEndPoint(handle, secondsForFrame(8));
    EXPECT(rig.engine.getLoopEndPoint(handle) == secondsForFrame(8),
           "getter should reflect the requested endpoint immediately");

    rig.mix(511);
    EXPECT(rig.source.framesRead.size() == 512,
           "cached source block should not be discarded after a live change");

    rig.mix(6);
    EXPECT(rig.source.framesRead.size() > 512,
           "the next source refill should decode additional frames");
    const std::vector<unsigned int> secondRefill(
        rig.source.framesRead.begin() + 512, rig.source.framesRead.end());
    expectLoopPattern(secondRefill, 7, 8, 5, 8, 6);
}

void testInaudibleTickUsesTheSameLoopBoundary() {
    std::printf("Test: inaudible tick uses the same loop boundary\n");
    LoopRig rig(64);
    rig.start(5, 8, true);
    rig.mix();

    expectLoopPattern(rig.source.framesRead, 0, 8, 5, 8, 60);
}

void testLoopStartPastEofStopsAfterNoProgress() {
    std::printf("Test: loop start past EOF cannot spin forever\n");
    LoopRig rig(4);
    rig.start(5, 8);
    rig.mix();

    EXPECT(rig.source.framesRead.size() == 4,
           "expected four source frames before EOF, got %zu",
           rig.source.framesRead.size());
    EXPECT(rig.source.seekCount <= 4,
           "zero-progress loop sought %u times", rig.source.seekCount);
}

}  // namespace

int main() {
    testShortLoopDoesNotCrossEndPoint();
    testExactRefillBoundaryLoopsBeforeNextFrame();
    testEndBeyondSourceFallsBackToNaturalEof();
    testClearingEndPointRestoresNaturalEof();
    testInvalidNativeRangeIsSafelyCleared();
    testMovingStartPastEndClearsEndPoint();
    testPublicSeekSynchronizesLoopCursor();
    testCommonSampleRateDoesNotCrossEndPoint();
    testCommonSampleRateSeekSynchronizesLoopCursor();
    testLiveEndPointChangeAppliesAtNextRefill();
    testInaudibleTickUsesTheSameLoopBoundary();
    testLoopStartPastEofStopsAfterNoProgress();

    if (gFailures != 0) {
        std::fprintf(stderr, "\n%d of %d assertions failed.\n",
                     gFailures, gAssertions);
        return 1;
    }

    std::printf("\nAll %d assertions passed.\n", gAssertions);
    return 0;
}

// Standalone native tests for NativeStream, the LoadMode.disk streaming
// source for M4A/MP4 (src/native_stream/).
//
// Runs on the null driver and drives the mixer by hand with Soloud::mix.
// Voices play at unity gain (point resampler, no clipping scaler), so the
// mixer output can be compared sample by sample with the expected PCM.
//
//   * A synthetic decoder backend whose sample values encode their own frame
//     index checks the ring, seek, loop and underrun logic exactly, and can
//     block or slow down inside read() to test teardown while decoding.
//   * On Apple, the ExtAudioFile backend is compared against the full decode
//     (NativeAudioDecoder::decodeFile) of the same files: length, sequential
//     PCM, seeks, looping and simultaneous voices.
//   * The container gapless parser is checked against the encoder delay and
//     length of the bundled assets.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_native_stream_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "native_stream/m4a_gapless.h"
#include "native_stream/native_stream_source.h"
#include "native_audio_decoder.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__ANDROID__)
#include <dlfcn.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kBufferSize = 512;
constexpr unsigned int kChannels = 2;

int gFailures = 0;
int gAssertions = 0;
std::string gAssetDir = "example/assets/audio";
// Optional generated files that decode deterministically: Apple's encoder
// (iTunSMPB) and ffmpeg's without noise substitution (edit list). ffmpeg's
// default PNS decodes to slightly different noise depending on where
// decoding starts, so the bundled sample-AAC.m4a is only compared loosely.
std::string gAppleEncoded;
std::string gFfmpegEncoded;
std::string gAc3InMp4; // optional ffmpeg AC-3 in MP4 (edit list delay 256)
std::string gNoEditList; // optional ffmpeg AAC in MP4 without an edit list

#define EXPECT(condition, format, ...) do { \
    ++gAssertions; \
    if (!(condition)) { \
        ++gFailures; \
        std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", \
                     __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs) {
    const auto start = Clock::now();
    while (!condition()) {
        if (msSince(start) > timeoutMs) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

bool waitForWorkersToExit() {
    return waitFor([] { return SoLoud::NativeStream::liveWorkerCount() == 0; }, 5000);
}

// ---------------------------------------------------------------------------
// Synthetic backend: channel 0 of frame f is (f + 1) * kStep, channel 1 is
// its negation, so any output sample says exactly which frame it came from.
// ---------------------------------------------------------------------------

constexpr float kStep = 1.0f / 1048576.0f; // exact for frames < 2^24

float syntheticValue(uint64_t frame, unsigned int channel) {
    const float v = static_cast<float>(frame + 1) * kStep; // frame 0 isn't silence
    return channel == 0 ? v : -v;
}

struct SyntheticControl {
    std::atomic<int> readDelayUs{0};
    std::atomic<int> opened{0};
    std::atomic<int> destroyed{0};
    std::atomic<int> inRead{0};
    std::atomic<int> reads{0};
    // When closed, read() blocks until it is opened again.
    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool gateOpen = true;
    // Fail every open after the first (the load) to test a missing file.
    bool failWorkerOpen = false;
    // read() fails once it reaches this frame (-1: never).
    std::atomic<long long> failReadAt{-1};
    // When set, decoders after the first (the load) report this length or
    // rate, as a file replaced between load and play would.
    uint64_t workerTotalFrames = 0;
    float workerSampleRate = 0.0f;

    void closeGate() {
        std::lock_guard<std::mutex> lock(gateMutex);
        gateOpen = false;
    }
    void openGate() {
        {
            std::lock_guard<std::mutex> lock(gateMutex);
            gateOpen = true;
        }
        gateCv.notify_all();
    }
};

class SyntheticDecoder : public NativeStreamDecoder {
public:
    SyntheticDecoder(SyntheticControl &control, uint64_t totalFrames) : mControl(control) {
        const bool worker = mControl.opened++ > 0;
        mFormat.channels = 2;
        mFormat.sampleRate = worker && control.workerSampleRate > 0 ? control.workerSampleRate
                                                                     : static_cast<float>(kSampleRate);
        mFormat.totalFrames = worker && control.workerTotalFrames > 0 ? control.workerTotalFrames : totalFrames;
    }
    ~SyntheticDecoder() override { mControl.destroyed++; }

    const NativeStreamFormat &format() const override { return mFormat; }

    bool seek(uint64_t frame) override {
        mPosition = frame < mFormat.totalFrames ? frame : mFormat.totalFrames;
        return true;
    }

    long read(float *out, unsigned int maxFrames) override {
        mControl.reads++;
        mControl.inRead++;
        {
            std::unique_lock<std::mutex> lock(mControl.gateMutex);
            mControl.gateCv.wait(lock, [&] { return mControl.gateOpen; });
        }
        const int delay = mControl.readDelayUs.load();
        if (delay > 0) std::this_thread::sleep_for(std::chrono::microseconds(delay));
        mControl.inRead--;
        const long long failAt = mControl.failReadAt.load();
        if (failAt >= 0 && mPosition >= static_cast<uint64_t>(failAt)) return -1;
        if (failAt >= 0 && mPosition + maxFrames > static_cast<uint64_t>(failAt))
            maxFrames = static_cast<unsigned int>(failAt - static_cast<long long>(mPosition));
        unsigned int n = 0;
        for (; n < maxFrames && mPosition < mFormat.totalFrames; n++, mPosition++) {
            out[n * 2] = syntheticValue(mPosition, 0);
            out[n * 2 + 1] = syntheticValue(mPosition, 1);
        }
        return static_cast<long>(n);
    }

private:
    SyntheticControl &mControl;
    NativeStreamFormat mFormat;
    uint64_t mPosition = 0;
};

NativeStreamDecoderFactory syntheticFactory(SyntheticControl &control, uint64_t totalFrames) {
    return [&control, totalFrames](const NativeStreamFile &, std::string &error) -> std::unique_ptr<NativeStreamDecoder> {
        if (control.failWorkerOpen && control.opened.load() > 0) {
            error = "synthetic open failure";
            return nullptr;
        }
        return std::make_unique<SyntheticDecoder>(control, totalFrames);
    };
}

// ---------------------------------------------------------------------------
// Engine helpers
// ---------------------------------------------------------------------------

bool initEngine(SoLoud::Soloud &soloud) {
    const SoLoud::result r = soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate, kBufferSize, kChannels);
    EXPECT(r == SoLoud::SO_NO_ERROR, "soloud.init failed: %d", r);
    if (r != SoLoud::SO_NO_ERROR) return false;
    soloud.setPostClipScaler(1.0f);
    soloud.setMainResampler(SoLoud::Soloud::RESAMPLER_POINT);
    return true;
}

SoLoud::AudioSourceInstance *voiceOf(SoLoud::Soloud &soloud, SoLoud::handle h) {
    const int v = soloud.getVoiceFromHandle_internal(h);
    return v >= 0 ? soloud.mVoice[v] : nullptr;
}

/// Starts [source] paused at unity gain on both output channels (no pan law,
/// no start-up volume ramp), so mixer output equals the source PCM.
SoLoud::handle playUnityPaused(SoLoud::Soloud &soloud, SoLoud::AudioSource &source, float volume = 1.0f) {
    SoLoud::handle h = soloud.play(source, volume, 0.0f, true);
    EXPECT(h != 0, "play failed");
    soloud.setPanAbsolute(h, 1.0f, 1.0f);
    soloud.lockAudioMutex_internal();
    SoLoud::AudioSourceInstance *voice = voiceOf(soloud, h);
    if (voice) {
        for (unsigned int c = 0; c < MAX_CHANNELS; c++) {
            voice->mCurrentChannelVolume[c] = voice->mChannelVolume[c] * voice->mOverallVolume;
        }
    }
    soloud.unlockAudioMutex_internal();
    return h;
}

/// Plays [source] at unity gain (see [playUnityPaused]).
SoLoud::handle playUnity(SoLoud::Soloud &soloud, SoLoud::AudioSource &source, float volume = 1.0f) {
    SoLoud::handle h = playUnityPaused(soloud, source, volume);
    soloud.setPause(h, false);
    return h;
}

/// Buffered frames of a NativeStream voice, 0 if it has none.
uint64_t buffered(SoLoud::Soloud &soloud, SoLoud::handle h) {
    soloud.lockAudioMutex_internal();
    auto *voice = dynamic_cast<SoLoud::NativeStreamInstance *>(voiceOf(soloud, h));
    const uint64_t frames = voice ? voice->bufferedFrames() : 0;
    soloud.unlockAudioMutex_internal();
    return frames;
}

/// Mixes [frames] frames in kBufferSize blocks. Before each block it gives
/// the voices' workers time to buffer ahead (as real-time playback would),
/// up to [paceMs], so the comparisons don't depend on scheduling.
std::vector<float> mixPaced(SoLoud::Soloud &soloud, const std::vector<SoLoud::handle> &voices, size_t frames,
                            int paceMs = 2000) {
    std::vector<float> out(frames * kChannels);
    size_t done = 0;
    while (done < frames) {
        const unsigned int n = static_cast<unsigned int>(std::min<size_t>(kBufferSize, frames - done));
        if (paceMs > 0) {
            for (SoLoud::handle h : voices) {
                if (!soloud.isValidVoiceHandle(h)) continue;
                // Ready when a block is buffered, or the worker idles (ring
                // full, head-only sound, end of stream, or no such voice).
                uint64_t last = ~0ull;
                const auto start = Clock::now();
                auto idleSince = start;
                while (msSince(start) < paceMs) {
                    const uint64_t b = buffered(soloud, h);
                    if (b >= 4 * kBufferSize) break;
                    if (b != last) {
                        last = b;
                        idleSince = Clock::now();
                    } else if (msSince(idleSince) > (b == 0 ? 1000 : 100)) {
                        // Nothing buffered may be a codec still starting up
                        // (slow on emulators); a partly filled ring that
                        // stopped growing is the end of the stream.
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        soloud.mix(out.data() + done * kChannels, n);
        done += n;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Synthetic-backend tests (all platforms)
// ---------------------------------------------------------------------------

/// Returns the frame index a mixed stereo sample pair encodes, or -1 for
/// silence / a pair that doesn't match the synthetic pattern.
long long frameOf(const float *pair, float gain = 1.0f) {
    const float l = pair[0] / gain;
    const float r = pair[1] / gain;
    if (l == 0.0f && r == 0.0f) return -1;
    if (std::fabs(l + r) > 1e-9f) return -2;
    return std::llround(l / kStep) - 1;
}

/// Checks that [out] holds consecutive frames [first, first + count).
bool expectFrames(const std::vector<float> &out, size_t offset, uint64_t first, size_t count, const char *what) {
    for (size_t i = 0; i < count; i++) {
        const long long f = frameOf(&out[(offset + i) * kChannels]);
        if (f != static_cast<long long>(first + i)) {
            EXPECT(false, "%s: output frame %zu holds source frame %lld, expected %llu", what, offset + i, f,
                   static_cast<unsigned long long>(first + i));
            return false;
        }
    }
    ++gAssertions;
    return true;
}

void testSyntheticPlayback() {
    std::printf("synthetic: plays every frame once, then ends\n");
    SyntheticControl control;
    const uint64_t total = 3 * kSampleRate + 123;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        EXPECT(stream.load("synthetic", syntheticFactory(control, total)) == SoLoud::SO_NO_ERROR, "load failed");
        EXPECT(stream.getFrameCount() == total, "frame count %llu", (unsigned long long)stream.getFrameCount());
        EXPECT(std::fabs(stream.getLength() - double(total) / kSampleRate) < 1e-9, "length %f", stream.getLength());
        EXPECT(control.opened == 1 && control.destroyed == 1, "load should open and close one decoder");

        SoLoud::handle h = playUnity(soloud, stream);
        std::vector<float> out = mixPaced(soloud, {h}, total + 2048);
        expectFrames(out, 0, 0, total, "full playback");
        bool silentTail = true;
        for (size_t i = total * kChannels; i < out.size(); i++) silentTail &= out[i] == 0.0f;
        EXPECT(silentTail, "output after the end should be silent");
        EXPECT(!soloud.isValidVoiceHandle(h), "voice should end at the stream end");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running: %d", SoLoud::NativeStream::liveWorkerCount());
    EXPECT(control.opened == control.destroyed, "decoders leaked: %d opened, %d destroyed", control.opened.load(),
           control.destroyed.load());
}

void testSyntheticSeek() {
    std::printf("synthetic: seeks land on the exact frame (head, ring, forward, backward)\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        mixPaced(soloud, {h}, 4096);

        const double targets[] = {5.0, 0.25, 7.123, 6.9, 0.0, 9.5, 0.7499, 0.75, 1.2, 1.25};
        for (double t : targets) {
            soloud.seek(h, t);
            const uint64_t first = static_cast<uint64_t>(std::floor(t * kSampleRate));
            std::vector<float> out = mixPaced(soloud, {h}, 3 * kBufferSize);
            char what[64];
            std::snprintf(what, sizeof(what), "seek to %.4f s", t);
            expectFrames(out, 0, first, 3 * kBufferSize, what);
            EXPECT(std::fabs(soloud.getStreamPosition(h) - (t + 3.0 * kBufferSize / kSampleRate)) < 1e-6,
                   "%s: position %f", what, soloud.getStreamPosition(h));
        }
        // Seeking past the end ends the voice.
        soloud.seek(h, 11.0);
        mixPaced(soloud, {h}, kBufferSize);
        EXPECT(!soloud.isValidVoiceHandle(h), "voice should end after seeking past the end");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticLooping() {
    std::printf("synthetic: gapless loops (whole file, loop point, loop end point)\n");
    SyntheticControl control;
    const uint64_t total = 2 * kSampleRate + 777;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));

        // Whole file, three times round.
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        std::vector<float> out = mixPaced(soloud, {h}, 3 * total);
        for (int lap = 0; lap < 3; lap++) {
            char what[32];
            std::snprintf(what, sizeof(what), "loop lap %d", lap);
            expectFrames(out, lap * total, 0, total, what);
        }
        // The mixer reads a block ahead, so it may already be in lap 4.
        EXPECT(soloud.getLoopCount(h) == 2 || soloud.getLoopCount(h) == 3, "loop count %u", soloud.getLoopCount(h));
        soloud.stop(h);

        // Loop point beyond the head cache.
        const uint64_t loopStart = kSampleRate + 1000;
        h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        soloud.setLoopPoint(h, double(loopStart) / kSampleRate);
        out = mixPaced(soloud, {h}, total + 2 * (total - loopStart));
        expectFrames(out, 0, 0, total, "loop point lap 0");
        expectFrames(out, total, loopStart, total - loopStart, "loop point lap 1");
        expectFrames(out, total + (total - loopStart), loopStart, total - loopStart, "loop point lap 2");
        soloud.stop(h);

        // Bounded loop [0.5 s, 1.5 s): starts in the head cache, ends in
        // the ring.
        const uint64_t start = kSampleRate / 2;
        const uint64_t end = kSampleRate + kSampleRate / 2;
        h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        soloud.setLoopPoint(h, double(start) / kSampleRate);
        soloud.setLoopEndPoint(h, double(end) / kSampleRate);
        out = mixPaced(soloud, {h}, end + 3 * (end - start));
        expectFrames(out, 0, 0, end, "bounded loop lap 0");
        for (int lap = 0; lap < 3; lap++) {
            char what[40];
            std::snprintf(what, sizeof(what), "bounded loop lap %d", lap + 1);
            expectFrames(out, end + lap * (end - start), start, end - start, what);
        }
        soloud.stop(h);
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticLoopsBeyondHead() {
    std::printf("synthetic: loops starting past the head cache wrap gaplessly (time and frame offsets)\n");
    SyntheticControl control;
    const uint64_t total = 4 * kSampleRate;
    // A start frame whose seconds round-trip (frame / rate * rate) lands
    // just below it, as frame-offset loop starts reach seek().
    uint64_t frameStart = kSampleRate + 1000;
    while ((double(frameStart) / kSampleRate) * kSampleRate >= double(frameStart)) frameStart++;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));

        // Bounded loop entirely past the head: every lap must come from the
        // worker's ahead-of-time wrap, with no underrun.
        const uint64_t start = kSampleRate + 1000;
        const uint64_t end = 2 * kSampleRate + 500;
        const uint64_t underruns = SoLoud::NativeStream::underrunFrameCount();
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        soloud.setLoopPoint(h, double(start) / kSampleRate);
        soloud.setLoopEndPoint(h, double(end) / kSampleRate);
        std::vector<float> out = mixPaced(soloud, {h}, end + 3 * (end - start));
        expectFrames(out, 0, 0, end, "bounded loop past head lap 0");
        for (int lap = 0; lap < 3; lap++) {
            char what[48];
            std::snprintf(what, sizeof(what), "bounded loop past head lap %d", lap + 1);
            expectFrames(out, end + lap * (end - start), start, end - start, what);
        }
        EXPECT(SoLoud::NativeStream::underrunFrameCount() == underruns, "bounded loop underran by %llu frames",
               (unsigned long long)(SoLoud::NativeStream::underrunFrameCount() - underruns));
        soloud.stop(h);

        // Loop start given as an exact frame offset.
        h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        soloud.lockAudioMutex_internal();
        if (SoLoud::AudioSourceInstance *voice = voiceOf(soloud, h)) voice->mLoopStartFrame = (long long)frameStart;
        soloud.unlockAudioMutex_internal();
        out = mixPaced(soloud, {h}, total + 2 * (total - frameStart));
        expectFrames(out, 0, 0, total, "frame-offset loop lap 0");
        expectFrames(out, total, frameStart, total - frameStart, "frame-offset loop lap 1");
        expectFrames(out, total + (total - frameStart), frameStart, total - frameStart, "frame-offset loop lap 2");
        soloud.stop(h);
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticVoices() {
    std::printf("synthetic: simultaneous voices of one source are independent\n");
    SyntheticControl control;
    const uint64_t total = 5 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        // Four voices at offsets 0, 1, 2 and 3 s; channel 0 of the mix is
        // the sum of their frame values.
        std::vector<SoLoud::handle> voices;
        for (int i = 0; i < 4; i++) {
            SoLoud::handle h = playUnity(soloud, stream, 0.25f);
            soloud.seek(h, double(i));
            voices.push_back(h);
        }
        EXPECT(SoLoud::NativeStream::liveWorkerCount() == 4, "workers %d", SoLoud::NativeStream::liveWorkerCount());
        const size_t frames = kSampleRate * 3 / 2;
        std::vector<float> out = mixPaced(soloud, voices, frames);
        double maxErr = 0;
        for (size_t i = 0; i < frames; i++) {
            double expected = 0;
            for (int v = 0; v < 4; v++) expected += 0.25 * syntheticValue(v * kSampleRate + i, 0);
            maxErr = std::max(maxErr, std::fabs(out[i * kChannels] - expected));
        }
        EXPECT(maxErr < 1e-6, "mixed voices differ from the expected sum by %g", maxErr);
        for (SoLoud::handle h : voices) soloud.stop(h);
        EXPECT(waitForWorkersToExit(), "stopped voices' workers should exit");
    }
    soloud.deinit();
}

void testSyntheticColdSeekWaits() {
    std::printf("synthetic: a seek outside the buffer waits for its frames instead of skipping them\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        mixPaced(soloud, {h}, 4096);

        // A realistic decoder, mixed right after the seek in real-time-ish
        // steps with no prebuffering: every frame from the target on plays,
        // after a short wait.
        control.readDelayUs = 2000;
        for (double t : {6.0, 3.5, 8.25}) {
            soloud.seek(h, t);
            const uint64_t first = static_cast<uint64_t>(std::floor(t * kSampleRate));
            std::vector<float> out;
            size_t leading = 0;
            bool started = false;
            const size_t blocks = 40;
            for (size_t b = 0; b < blocks; b++) {
                std::vector<float> block(kBufferSize * kChannels);
                soloud.mix(block.data(), kBufferSize);
                for (unsigned int i = 0; i < kBufferSize; i++) {
                    if (!started && frameOf(&block[i * kChannels]) == -1) {
                        leading++;
                        continue;
                    }
                    started = true;
                    out.insert(out.end(), &block[i * kChannels], &block[(i + 1) * kChannels]);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            char what[64];
            std::snprintf(what, sizeof(what), "cold seek to %.2f s", t);
            EXPECT(started, "%s: nothing played", what);
            EXPECT(leading < kSampleRate / 4, "%s: waited %zu frames", what, leading);
            expectFrames(out, 0, first, out.size() / kChannels, what);
            // The position held while waiting: it matches what was played.
            const double expected = t + double(out.size() / kChannels) / kSampleRate;
            EXPECT(std::fabs(soloud.getStreamPosition(h) - expected) < 1e-6, "%s: position %f, expected %f", what,
                   soloud.getStreamPosition(h), expected);
        }

        // A decoder far slower than real time: mixing never blocks, and the
        // voice holds the seek target until its frames arrive.
        control.readDelayUs = 200000;
        soloud.seek(h, 2.0);
        const auto start = Clock::now();
        std::vector<float> out = mixPaced(soloud, {h}, 8 * kBufferSize, 0);
        EXPECT(msSince(start) < 100, "mixing must not wait for the decoder (took %.1f ms)", msSince(start));
        bool silent = true;
        for (size_t i = 0; i < 8 * kBufferSize; i++) {
            if (frameOf(&out[i * kChannels]) != -1) silent = false;
        }
        EXPECT(silent, "nothing should play before the decoder delivers the target");
        EXPECT(std::fabs(soloud.getStreamPosition(h) - 2.0) < 1e-6, "position should hold at the target: %f",
               soloud.getStreamPosition(h));
        control.readDelayUs = 0;
        out = mixPaced(soloud, {h}, 3 * kBufferSize);
        expectFrames(out, 0, 2 * kSampleRate, 3 * kBufferSize, "after the wait");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticUnderrun() {
    std::printf("synthetic: an underrun mid-playback plays silence and keeps time\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.seek(h, 2.0);
        std::vector<float> out = mixPaced(soloud, {h}, 4 * kBufferSize);
        expectFrames(out, 0, 2 * kSampleRate, 4 * kBufferSize, "before the underrun");
        uint64_t played = 4 * kBufferSize;

        // ~2048 frames per 200 ms: far slower than real time. Drain the ring.
        control.readDelayUs = 200000;
        const auto start = Clock::now();
        while (buffered(soloud, h) > 0 && played < 4 * kSampleRate) {
            out = mixPaced(soloud, {h}, kBufferSize, 0);
            played += kBufferSize;
        }
        EXPECT(buffered(soloud, h) == 0, "the ring should have drained");
        const uint64_t underrunsBefore = SoLoud::NativeStream::underrunFrameCount();
        out = mixPaced(soloud, {h}, 8 * kBufferSize, 0);
        EXPECT(msSince(start) < 1000, "mixing must not wait for the decoder (took %.1f ms)", msSince(start));
        EXPECT(SoLoud::NativeStream::underrunFrameCount() > underrunsBefore, "expected underrun frames");
        // Whatever arrived is the right frame for its position.
        bool inSync = true;
        for (size_t i = 0; i < 8 * kBufferSize; i++) {
            const long long f = frameOf(&out[i * kChannels]);
            if (f != -1 && f != static_cast<long long>(2 * kSampleRate + played + i)) inSync = false;
        }
        EXPECT(inSync, "underrun output must be silence or the frame for that position");
        played += 8 * kBufferSize;
        EXPECT(std::fabs(soloud.getStreamPosition(h) - (2.0 + double(played) / kSampleRate)) < 1e-6,
               "position kept moving: %f", soloud.getStreamPosition(h));

        // Once the decoder is fast again, playback recovers in sync.
        control.readDelayUs = 0;
        EXPECT(waitFor([&] { return buffered(soloud, h) >= kSampleRate / 4; }, 3000), "decoder should catch up");
        out = mixPaced(soloud, {h}, 3 * kBufferSize);
        expectFrames(out, 0, 2 * kSampleRate + played, 3 * kBufferSize, "after recovery");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

// Render-ahead engine like the retroactive re-mix tests: the test plays the
// device, reading the ring by hand.
constexpr unsigned int kRenderAhead = 4096;
constexpr unsigned int kDevicePeriod = 512;

bool initRenderAheadEngine(SoLoud::Soloud &soloud) {
    soloud.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
    if (!initEngine(soloud)) return false;
    EXPECT(soloud.isRenderAheadEnabled(), "render-ahead ring must be enabled");
    return soloud.isRenderAheadEnabled();
}

/// Tops the ring up (after letting the workers buffer, as real time would)
/// and reads [frames] frames from it, like the device callback.
std::vector<float> playDevice(SoLoud::Soloud &soloud, SoLoud::handle h, size_t frames) {
    std::vector<float> out(frames * kChannels);
    size_t done = 0;
    while (done < frames) {
        waitFor([&] { return !soloud.isValidVoiceHandle(h) || buffered(soloud, h) >= kSampleRate / 4; }, 2000);
        soloud.renderRingTopUp_internal(); // locks the engine itself
        const unsigned int n = static_cast<unsigned int>(std::min<size_t>(kBufferSize, frames - done));
        soloud.mRenderRing.read(out.data() + done * kChannels, n);
        done += n;
    }
    return out;
}

/// Rolls the engine back to the checkpoint at the playhead and re-mixes the
/// ring, as a retroactive event does. Returns false when it degraded.
bool rollBackToPlayhead(SoLoud::Soloud &soloud) {
    soloud.lockAudioMutex_internal();
    const int ci = soloud.retroactiveBegin_internal(soloud.playheadTimeLocked_internal());
    if (ci >= 0) soloud.retroactiveEnd_internal(ci);
    soloud.unlockAudioMutex_internal();
    return ci >= 0;
}

void testSyntheticRetroactiveRemix() {
    std::printf("synthetic: checkpoints rewind a streaming voice exactly (retroactive re-mix)\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initRenderAheadEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        // The ring holds frames mixed before the voice started; skip them.
        std::vector<float> out = playDevice(soloud, h, kRenderAhead + 2 * kBufferSize);
        size_t skip = 0;
        while (skip < out.size() / kChannels && frameOf(&out[skip * kChannels]) == -1) skip++;
        std::vector<float> played(out.begin() + skip * kChannels, out.end());

        // Roll back repeatedly: in the head cache, at its end, and well into
        // the ring. Every rollback is exact, so the device hears every frame
        // once, in order.
        int rollbacks = 0;
        while (played.size() / kChannels < 2 * kSampleRate) {
            EXPECT(rollBackToPlayhead(soloud), "rollback at %zu should be restorable", played.size() / kChannels);
            rollbacks++;
            out = playDevice(soloud, h, 8 * kBufferSize);
            played.insert(played.end(), out.begin(), out.end());
        }
        EXPECT(rollbacks > 0, "no rollbacks");
        expectFrames(played, 0, 0, played.size() / kChannels, "across rollbacks");

        // A seek starts a new request: checkpoints from before it can no
        // longer be restored, and the rollback degrades instead.
        soloud.seek(h, 6.0);
        EXPECT(!rollBackToPlayhead(soloud), "rollback across a seek must degrade");
        // The voice carries on unharmed, from the seek target.
        out = playDevice(soloud, h, kRenderAhead + 4 * kBufferSize);
        std::vector<float> after;
        bool reached = false;
        for (size_t i = 0; i < out.size() / kChannels; i++) {
            if (frameOf(&out[i * kChannels]) == static_cast<long long>(6 * kSampleRate)) reached = true;
            if (reached) after.insert(after.end(), &out[i * kChannels], &out[(i + 1) * kChannels]);
        }
        EXPECT(reached, "the voice should play from the seek target");
        expectFrames(after, 0, 6 * kSampleRate, after.size() / kChannels, "after the seek");

        // And once the window is past the seek, rollbacks work again.
        EXPECT(rollBackToPlayhead(soloud), "rollback after the seek should be restorable");
        out = playDevice(soloud, h, 4 * kBufferSize);
        expectFrames(out, 0, 6 * kSampleRate + after.size() / kChannels, 4 * kBufferSize, "rollback after the seek");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticHistoryBounds() {
    std::printf("synthetic: rollbacks past a voice's history degrade, and history never starves the worker\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initRenderAheadEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.seek(h, 3.0);
        playDevice(soloud, h, kSampleRate / 2);

        soloud.lockAudioMutex_internal();
        auto *voice = dynamic_cast<SoLoud::NativeStreamInstance *>(voiceOf(soloud, h));
        SoLoud::SourceStateSnapshot *snapshot = voice ? voice->captureSourceState() : nullptr;
        soloud.unlockAudioMutex_internal();
        EXPECT(snapshot != nullptr, "no snapshot");
        if (snapshot) {
            soloud.lockAudioMutex_internal();
            EXPECT(voice->canRestoreSourceState(snapshot), "a fresh snapshot is restorable");
            soloud.unlockAudioMutex_internal();
            // Play on well past the history (a few render-ahead windows).
            playDevice(soloud, h, kSampleRate / 2);
            soloud.lockAudioMutex_internal();
            EXPECT(!voice->canRestoreSourceState(snapshot), "a snapshot older than the history is not");
            soloud.unlockAudioMutex_internal();
            delete snapshot;
        }
        // The read-ahead stayed full (a second) with the history kept.
        EXPECT(waitFor([&] { return buffered(soloud, h) >= kSampleRate * 9 / 10; }, 3000),
               "read-ahead should fill: %llu", static_cast<unsigned long long>(buffered(soloud, h)));

        // Many short loop wraps (one marker each) can't fill the markers
        // through the history: the worker keeps buffering and nothing
        // underruns.
        // Far from where the voice is now (~4 s), so only the loop's own
        // frames match.
        const double loopStart = 8.0;
        const double loopEnd = 8.0 + 300.0 / kSampleRate;
        soloud.setLoopPoint(h, loopStart);
        soloud.setLoopEndPoint(h, loopEnd);
        soloud.setLooping(h, true);
        soloud.seek(h, loopStart);
        const uint64_t underrunsBefore = SoLoud::NativeStream::underrunFrameCount();
        std::vector<float> out = playDevice(soloud, h, kRenderAhead + kSampleRate / 2);
        size_t first = 0;
        while (first < out.size() / kChannels &&
               frameOf(&out[first * kChannels]) != static_cast<long long>(8 * kSampleRate))
            first++;
        // A loop end in seconds rounds up to a whole frame: find the period.
        size_t period = 0;
        for (size_t i = first + 1; period == 0 && i < out.size() / kChannels; i++) {
            if (frameOf(&out[i * kChannels]) == static_cast<long long>(8 * kSampleRate)) period = i - first;
        }
        EXPECT(period == 300 || period == 301, "loop period %zu", period);
        bool looped = first < out.size() / kChannels && period > 0;
        for (size_t i = first; looped && i < out.size() / kChannels; i++) {
            if (frameOf(&out[i * kChannels]) != static_cast<long long>(8 * kSampleRate + (i - first) % period))
                looped = false;
        }
        EXPECT(looped, "short loop should play gaplessly from frame %zu", first);
        EXPECT(SoLoud::NativeStream::underrunFrameCount() - underrunsBefore < 2 * kBufferSize,
               "short loop underran %llu frames",
               static_cast<unsigned long long>(SoLoud::NativeStream::underrunFrameCount() - underrunsBefore));
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticTeardownWhileDecoding() {
    std::printf("synthetic: stop, source destruction and deinit never wait on a blocked decode\n");
    SyntheticControl control;
    const uint64_t total = 10 * kSampleRate;
    {
        SoLoud::Soloud soloud;
        if (!initEngine(soloud)) return;
        auto stream = std::make_unique<SoLoud::NativeStream>();
        stream->load("synthetic", syntheticFactory(control, total));

        // Block every worker inside read().
        control.closeGate();
        std::vector<SoLoud::handle> voices;
        for (int i = 0; i < 3; i++) voices.push_back(playUnity(soloud, *stream));
        mixPaced(soloud, voices, kSampleRate / 2, 0); // head cache still plays
        EXPECT(waitFor([&] { return control.inRead.load() == 3; }, 2000), "workers should be decoding (%d)",
               control.inRead.load());

        auto start = Clock::now();
        soloud.stop(voices[0]);
        EXPECT(msSince(start) < 20, "stop waited %.1f ms on a blocked decode", msSince(start));

        start = Clock::now();
        stream.reset(); // stops the other voices
        EXPECT(msSince(start) < 20, "source destruction waited %.1f ms", msSince(start));

        // A fresh voice of another source, blocked too, then engine deinit.
        SyntheticControl control2;
        stream = std::make_unique<SoLoud::NativeStream>();
        stream->load("synthetic", syntheticFactory(control2, total));
        control2.closeGate();
        playUnity(soloud, *stream);
        mixPaced(soloud, {}, kBufferSize, 0);
        EXPECT(waitFor([&] { return control2.inRead.load() == 1; }, 2000), "4th worker should be decoding");
        start = Clock::now();
        soloud.deinit();
        EXPECT(msSince(start) < 50, "deinit waited %.1f ms", msSince(start));
        stream.reset();

        EXPECT(SoLoud::NativeStream::liveWorkerCount() == 4, "blocked workers still alive: %d",
               SoLoud::NativeStream::liveWorkerCount());
        control.openGate();
        control2.openGate();
        EXPECT(waitForWorkersToExit(), "released workers should exit (%d left)",
               SoLoud::NativeStream::liveWorkerCount());
        EXPECT(control.opened == control.destroyed && control2.opened == control2.destroyed,
               "decoders leaked: %d/%d opened, %d/%d destroyed", control.opened.load(), control2.opened.load(),
               control.destroyed.load(), control2.destroyed.load());
    }

    std::printf("synthetic: rapid play/stop churn with a slow decoder\n");
    {
        SoLoud::Soloud soloud;
        if (!initEngine(soloud)) return;
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        control.readDelayUs = 3000;
        std::vector<float> out(kBufferSize * kChannels);
        for (int i = 0; i < 200; i++) {
            SoLoud::handle h = soloud.play(stream);
            soloud.seek(h, (i % 7) * 1.1);
            soloud.mix(out.data(), kBufferSize);
            if (i % 3) soloud.stop(h);
        }
        soloud.stopAll();
        soloud.deinit();
        control.readDelayUs = 0;
        EXPECT(waitForWorkersToExit(), "churned workers should exit (%d left)",
               SoLoud::NativeStream::liveWorkerCount());
        EXPECT(control.opened == control.destroyed, "decoders leaked: %d opened, %d destroyed",
               control.opened.load(), control.destroyed.load());
    }
}

void testSyntheticWorkerFailure() {
    std::printf("synthetic: a worker that can't open the file ends the voice after the head\n");
    SyntheticControl control;
    control.failWorkerOpen = true;
    const uint64_t total = 3 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        EXPECT(stream.load("synthetic", syntheticFactory(control, total)) == SoLoud::SO_NO_ERROR, "load failed");
        SoLoud::handle h = playUnity(soloud, stream);
        std::vector<float> out = mixPaced(soloud, {h}, kSampleRate);
        expectFrames(out, 0, 0, static_cast<size_t>(kSampleRate * 0.75), "head before failure");
        EXPECT(!soloud.isValidVoiceHandle(h), "voice should end when its worker fails");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

/// Mixes until [h] ends (or [maxFrames] are mixed) without pacing; returns
/// what was mixed.
std::vector<float> mixUntilEnded(SoLoud::Soloud &soloud, SoLoud::handle h, size_t maxFrames) {
    std::vector<float> out;
    std::vector<float> block(kBufferSize * kChannels);
    while (out.size() / kChannels < maxFrames && soloud.isValidVoiceHandle(h)) {
        soloud.mix(block.data(), kBufferSize);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

/// Counts output frames that hold source frame [frame].
size_t countFrame(const std::vector<float> &out, long long frame) {
    size_t n = 0;
    for (size_t i = 0; i < out.size() / kChannels; i++) {
        if (frameOf(&out[i * kChannels]) == frame) n++;
    }
    return n;
}

void testSyntheticFailureEndsLoopingVoice() {
    std::printf("synthetic: a failed worker ends looping voices too, after what it decoded\n");
    const uint64_t total = 3 * kSampleRate;
    const uint64_t head = static_cast<uint64_t>(std::ceil(kSampleRate * 0.75));
    const uint64_t errorAt = 3 * kSampleRate / 2;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    struct Case {
        const char *what;
        bool failOpen;
        bool looping;
        double loopPoint;
    };
    for (const Case &c : {Case{"open failure, looping from the start", true, true, 0.0},
                          Case{"open failure, loop point past the head", true, true, 2.0},
                          Case{"read error, looping from the start", false, true, 0.0},
                          Case{"read error, not looping", false, false, 0.0}}) {
        SyntheticControl control;
        control.failWorkerOpen = c.failOpen;
        if (!c.failOpen) control.failReadAt = static_cast<long long>(errorAt);
        SoLoud::NativeStream stream;
        EXPECT(stream.load("synthetic", syntheticFactory(control, total)) == SoLoud::SO_NO_ERROR, "load failed");
        SoLoud::handle h = playUnity(soloud, stream);
        if (c.looping) {
            soloud.setLoopPoint(h, c.loopPoint);
            soloud.setLooping(h, true);
        }
        // Start the worker, and let it fail (after decoding what it can).
        std::vector<float> block(kBufferSize * kChannels);
        soloud.mix(block.data(), kBufferSize);
        EXPECT(waitFor([] { return SoLoud::NativeStream::liveWorkerCount() == 0; }, 3000),
               "%s: worker should exit", c.what);
        std::vector<float> out = mixUntilEnded(soloud, h, 4 * total);
        out.insert(out.begin(), block.begin(), block.end());
        EXPECT(!soloud.isValidVoiceHandle(h), "%s: the voice should end", c.what);
        const uint64_t played = c.failOpen ? head : errorAt;
        expectFrames(out, 0, 0, played, c.what);
        EXPECT(countFrame(out, 0) == 1, "%s: frame 0 played %zu times", c.what, countFrame(out, 0));
        bool silentAfter = true;
        for (size_t i = played; i < out.size() / kChannels; i++) {
            if (frameOf(&out[i * kChannels]) != -1) silentAfter = false;
        }
        EXPECT(silentAfter, "%s: nothing should play after the failure", c.what);
        soloud.stopAll();
        EXPECT(waitForWorkersToExit(), "%s: workers still running", c.what);
    }
    soloud.deinit();
}

void testSyntheticWorkerFormatMismatch() {
    std::printf("synthetic: a worker whose file no longer matches the loaded one fails\n");
    const uint64_t total = 3 * kSampleRate;
    const uint64_t head = static_cast<uint64_t>(std::ceil(kSampleRate * 0.75));
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    for (int field = 0; field < 2; field++) {
        SyntheticControl control;
        if (field == 0) control.workerTotalFrames = total + 1000;
        else control.workerSampleRate = 48000.0f;
        const char *what = field == 0 ? "different length" : "different sample rate";
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        std::vector<float> out(kBufferSize * kChannels);
        soloud.mix(out.data(), kBufferSize);
        EXPECT(waitFor([] { return SoLoud::NativeStream::liveWorkerCount() == 0; }, 3000), "%s: worker should exit",
               what);
        std::vector<float> rest = mixUntilEnded(soloud, h, 2 * total);
        out.insert(out.end(), rest.begin(), rest.end());
        EXPECT(!soloud.isValidVoiceHandle(h), "%s: the voice should end", what);
        expectFrames(out, 0, 0, head, what);
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticIdleWorkerSleeps() {
    std::printf("synthetic: idle workers sleep instead of polling, and still refill in time\n");
    SyntheticControl control;
    const uint64_t total = 20 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        std::vector<float> out;
        std::vector<float> block(kBufferSize * kChannels);
        soloud.mix(block.data(), kBufferSize);
        out.insert(out.end(), block.begin(), block.end());
        EXPECT(waitFor([&] { return buffered(soloud, h) >= kSampleRate; }, 3000), "ring should fill");
        // A full ring: the worker waits, and wakes only on its long timeout
        // (it used to poll every 20 ms: ~50 wakeups here).
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const int readsBefore = control.reads.load();
        const uint64_t wakeupsBefore = SoLoud::NativeStream::workerWakeupCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        EXPECT(control.reads.load() == readsBefore, "an idle worker should not decode (%d reads)",
               control.reads.load() - readsBefore);
        EXPECT(SoLoud::NativeStream::workerWakeupCount() - wakeupsBefore <= 6, "idle worker woke %llu times in 1 s",
               static_cast<unsigned long long>(SoLoud::NativeStream::workerWakeupCount() - wakeupsBefore));

        // Real-time-ish playback: the voice wakes the worker as it drains,
        // so 4 s play with no underrun.
        const uint64_t underrunsBefore = SoLoud::NativeStream::underrunFrameCount();
        const auto start = Clock::now();
        for (size_t done = 0; done < 4 * kSampleRate; done += kBufferSize) {
            soloud.mix(block.data(), kBufferSize);
            out.insert(out.end(), block.begin(), block.end());
            // ~4x real time.
            const double due = 1000.0 * (done + kBufferSize) / kSampleRate / 4;
            const double now = msSince(start);
            if (due > now) std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>((due - now) * 1000)));
        }
        EXPECT(SoLoud::NativeStream::underrunFrameCount() == underrunsBefore, "underran %llu frames",
               static_cast<unsigned long long>(SoLoud::NativeStream::underrunFrameCount() - underrunsBefore));
        expectFrames(out, 0, 0, out.size() / kChannels, "paced playback");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testSyntheticPausedVoiceHoldsNoDecoder() {
    std::printf("synthetic: a voice opens its decoder only once it plays or seeks\n");
    SyntheticControl control;
    const uint64_t total = 5 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle paused = playUnityPaused(soloud, stream);
        SoLoud::handle seeked = playUnityPaused(soloud, stream);
        std::vector<float> block(kBufferSize * kChannels);
        soloud.mix(block.data(), kBufferSize);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        EXPECT(control.opened == 1, "paused voices opened %d decoders", control.opened.load() - 1);

        // A seek starts decoding (to have the target ready on unpause).
        soloud.seek(seeked, 2.0);
        EXPECT(waitFor([&] { return control.opened == 2; }, 2000), "a seek should open the decoder");
        EXPECT(waitFor([&] { return buffered(soloud, seeked) >= kSampleRate / 2; }, 3000), "seek target buffered");

        // So does playing; the head cache covers the codec start-up.
        soloud.setPause(paused, false);
        std::vector<float> out = mixPaced(soloud, {paused}, kSampleRate);
        EXPECT(control.opened == 3, "the playing voice should open its decoder (%d)", control.opened.load() - 1);
        expectFrames(out, 0, 0, kSampleRate, "a voice that started paused");
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

void testShortSoundHasNoWorker() {
    std::printf("synthetic: sounds shorter than the head cache start no worker\n");
    SyntheticControl control;
    const uint64_t total = kSampleRate / 4;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        EXPECT(SoLoud::NativeStream::liveWorkerCount() == 0, "no worker expected");
        std::vector<float> out = mixPaced(soloud, {h}, 2 * total);
        expectFrames(out, 0, 0, total, "short lap 0");
        expectFrames(out, total, 0, total, "short lap 1");
        EXPECT(control.opened == 1, "only the load should open a decoder");
    }
    soloud.deinit();
}

// ---------------------------------------------------------------------------
// Container parsing
// ---------------------------------------------------------------------------

void testGaplessParser() {
    std::printf("m4a_gapless: encoder delay and length of the bundled assets\n");
    struct Case {
        const char *file;
        double rate;
        uint64_t delay;
        uint64_t valid;
    } cases[] = {
        {"aac_lc_mono_44100.m4a", 44100, 1024, 88200},    // ffmpeg: edit list
        {"aac_lc_stereo_48000.m4a", 48000, 2112, 96000},  // Apple: iTunSMPB
        {"sample-AAC.m4a", 44100, 1024, 661500},
    };
    for (const Case &c : cases) {
        M4aGaplessInfo info;
        const std::string path = gAssetDir + "/" + c.file;
        EXPECT(parseM4aGapless(path, info), "%s: parse failed", c.file);
        uint64_t delay = 0, valid = 0;
        EXPECT(info.resolve(c.rate, delay, valid), "%s: no gapless info", c.file);
        EXPECT(delay == c.delay && valid == c.valid, "%s: delay %llu valid %llu, expected %llu %llu", c.file,
               (unsigned long long)delay, (unsigned long long)valid, (unsigned long long)c.delay,
               (unsigned long long)c.valid);
    }
    M4aGaplessInfo info;
    EXPECT(!parseM4aGapless(gAssetDir + "/sample-MP3.mp3", info), "an MP3 is not an MP4 container");

    // A box whose 64-bit size would wrap the file offset must be rejected,
    // not parsed in a loop forever.
    const char *tmp = std::getenv("TMPDIR");
    const std::string crafted = std::string(tmp ? tmp : "/tmp") + "/native_stream_wrap.m4a";
    if (FILE *f = std::fopen(crafted.c_str(), "wb")) {
        const unsigned char bytes[] = {0, 0, 0, 8, 'f', 't', 'y', 'p',            // ftyp, 8 bytes
                                       0, 0, 0, 1, 'f', 'r', 'e', 'e',            // free, 64-bit size:
                                       0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xF8}; // 2^64 - 8
        std::fwrite(bytes, 1, sizeof(bytes), f);
        std::fclose(f);
        const auto start = Clock::now();
        EXPECT(!parseM4aGapless(crafted, info), "a wrapping box size must be rejected");
        EXPECT(msSince(start) < 1000, "parsing the crafted file took %.0f ms", msSince(start));
        std::remove(crafted.c_str());
    }
    EXPECT(!parseM4aGapless(gAssetDir + "/does-not-exist.m4a", info), "missing file");
}

// ---------------------------------------------------------------------------
// Platform backend vs the full decode
// ---------------------------------------------------------------------------

#if NATIVE_STREAM_SUPPORTED

struct FullDecode {
    std::vector<float> interleaved;
    unsigned int channels = 0;
    float rate = 0;
    uint64_t frames = 0;
};

bool fullDecode(const std::string &path, FullDecode &out) {
    DecodedAudioData d = NativeAudioDecoder::decodeFile(path.c_str());
    if (!d.success) return false;
    out.channels = d.channels;
    out.rate = d.sampleRate;
    out.frames = d.sampleCount;
    out.interleaved.resize(d.sampleCount * d.channels);
    for (size_t i = 0; i < d.sampleCount; i++)
        for (unsigned int c = 0; c < d.channels; c++)
            out.interleaved[i * d.channels + c] = d.samples[c * d.sampleCount + i];
    d.freeSamples();

    // The container's declared length is the reference (Apple's afinfo
    // agrees). Older Android full decodes keep the AAC end padding when the
    // extractor reports no encoder-padding, so compare over the playable
    // part only and say so.
    M4aGaplessInfo gapless;
    uint64_t delay = 0, valid = 0;
    if (parseM4aGapless(path, gapless) && gapless.resolve(out.rate, delay, valid) && valid > 0 &&
        out.frames > valid && out.frames - valid <= 4096) {
        std::printf("  note: the full decode keeps %llu padding frames past the declared %llu; ignoring them\n",
                    (unsigned long long)(out.frames - valid), (unsigned long long)valid);
        out.frames = valid;
        out.interleaved.resize(valid * out.channels);
    }
    return true;
}

double maxDiff(const float *a, const float *b, size_t n) {
    double m = 0;
    for (size_t i = 0; i < n; i++) m = std::max(m, double(std::fabs(a[i] - b[i])));
    return m;
}

void testBackendAgainstFullDecode(const std::string &path, double tolerance) {
    std::printf("backend: %s matches the full decode (length, PCM, seeks)\n", path.c_str());
    FullDecode full;
    if (!fullDecode(path, full)) {
        EXPECT(false, "full decode of %s failed", path.c_str());
        return;
    }
    std::string error;
    auto decoder = openPlatformNativeStreamDecoder(path, error);
    EXPECT(decoder != nullptr, "open failed: %s", error.c_str());
    if (!decoder) return;
    const NativeStreamFormat fmt = decoder->format();
    EXPECT(fmt.channels == full.channels && fmt.sampleRate == full.rate, "format %u/%g vs %u/%g", fmt.channels,
           fmt.sampleRate, full.channels, full.rate);
    EXPECT(fmt.totalFrames == full.frames, "length %llu vs full decode %llu", (unsigned long long)fmt.totalFrames,
           (unsigned long long)full.frames);

    std::vector<float> streamed;
    std::vector<float> chunk(1000 * fmt.channels);
    long got;
    while ((got = decoder->read(chunk.data(), 1000)) > 0)
        streamed.insert(streamed.end(), chunk.begin(), chunk.begin() + got * fmt.channels);
    EXPECT(got == 0, "read error");
    EXPECT(streamed.size() == full.interleaved.size(), "streamed %zu samples vs %zu", streamed.size(),
           full.interleaved.size());
    if (streamed.size() == full.interleaved.size()) {
        const double d = maxDiff(streamed.data(), full.interleaved.data(), streamed.size());
        EXPECT(d < 1e-6, "sequential PCM differs by %g", d);
    }

    // Seek accuracy: a seek then a read must equal the full decode there.
    const double fractions[] = {0.5, 0.1, 0.9, 0.0, 0.33, 0.999};
    double worst = 0;
    for (double f : fractions) {
        const uint64_t frame = static_cast<uint64_t>(f * fmt.totalFrames);
        const unsigned int n = static_cast<unsigned int>(std::min<uint64_t>(4096, fmt.totalFrames - frame));
        EXPECT(decoder->seek(frame), "seek to %llu failed", (unsigned long long)frame);
        std::vector<float> buf(n * fmt.channels);
        unsigned int have = 0;
        while (have < n && (got = decoder->read(buf.data() + have * fmt.channels, n - have)) > 0) have += got;
        EXPECT(have == n, "read %u of %u frames after seek", have, n);
        worst = std::max(worst, maxDiff(buf.data(), full.interleaved.data() + frame * fmt.channels, have * fmt.channels));
    }
    EXPECT(worst < tolerance, "PCM after seeks differs by %g", worst);
    std::printf("  max |seek - full| = %g\n", worst);
}

void testStreamingMatchesFullDecode(const std::string &path, double tolerance) {
    std::printf("NativeStream: %s through the mixer matches the full decode\n", path.c_str());
    FullDecode full;
    if (!fullDecode(path, full) || full.rate != kSampleRate) {
        EXPECT(false, "needs a %u Hz full decode of %s", kSampleRate, path.c_str());
        return;
    }
    auto expected = [&](uint64_t frame, unsigned int c) {
        return full.interleaved[frame * full.channels + (full.channels == 1 ? 0 : c)];
    };
    auto compare = [&](const std::vector<float> &out, size_t offset, uint64_t first, size_t count, double gain = 1.0) {
        double m = 0;
        for (size_t i = 0; i < count; i++)
            for (unsigned int c = 0; c < kChannels; c++)
                m = std::max(m, double(std::fabs(out[(offset + i) * kChannels + c] - gain * expected(first + i, c))));
        return m;
    };

    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        auto start = Clock::now();
        EXPECT(stream.load(path.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
        const double loadMs = msSince(start);
        EXPECT(stream.getFrameCount() == full.frames, "length %llu vs %llu",
               (unsigned long long)stream.getFrameCount(), (unsigned long long)full.frames);
        std::printf("  load %.1f ms for %.1f s of audio\n", loadMs, double(full.frames) / full.rate);

        const uint64_t underruns = SoLoud::NativeStream::underrunFrameCount();
        SoLoud::handle h = playUnity(soloud, stream);
        std::vector<float> out = mixPaced(soloud, {h}, full.frames + kBufferSize);
        double d = compare(out, 0, 0, full.frames);
        EXPECT(d < tolerance, "playback differs from the full decode by %g", d);
        EXPECT(!soloud.isValidVoiceHandle(h), "voice should end with the stream");
        EXPECT(SoLoud::NativeStream::underrunFrameCount() == underruns, "unexpected underrun");

        // Seeks through the mixer.
        h = playUnity(soloud, stream);
        const double targets[] = {7.5, 0.3, 12.25, 2.0, 0.76};
        double worst = 0;
        for (double t : targets) {
            const uint64_t frame = static_cast<uint64_t>(std::floor(t * kSampleRate));
            if (frame + 4096 > full.frames) continue;
            soloud.seek(h, t);
            out = mixPaced(soloud, {h}, 4096);
            worst = std::max(worst, compare(out, 0, frame, 4096));
        }
        EXPECT(worst < tolerance, "mixer seeks differ from the full decode by %g", worst);
        soloud.stop(h);

        // Looping: the wrap is gapless, so a lap boundary is the end of the
        // file followed directly by its start.
        h = playUnity(soloud, stream);
        soloud.setLooping(h, true);
        const double t0 = double(full.frames - kSampleRate) / kSampleRate;
        soloud.seek(h, t0);
        out = mixPaced(soloud, {h}, 2 * kSampleRate);
        d = std::max(compare(out, 0, full.frames - kSampleRate, kSampleRate),
                     compare(out, kSampleRate, 0, kSampleRate));
        EXPECT(d < tolerance, "loop wrap differs from end + start by %g", d);
        soloud.stop(h);

        // Simultaneous voices at different offsets.
        std::vector<SoLoud::handle> voices;
        const double offsets[] = {0.0, 1.5, 4.0};
        for (double o : offsets) {
            SoLoud::handle v = playUnity(soloud, stream, 1.0f / 3);
            soloud.seek(v, o);
            voices.push_back(v);
        }
        const size_t frames = kSampleRate * 2;
        const uint64_t underrunsBefore = SoLoud::NativeStream::underrunFrameCount();
        out = mixPaced(soloud, voices, frames);
        EXPECT(SoLoud::NativeStream::underrunFrameCount() == underrunsBefore, "three voices underran by %llu frames",
               (unsigned long long)(SoLoud::NativeStream::underrunFrameCount() - underrunsBefore));
        d = 0;
        for (size_t i = 0; i < frames; i++)
            for (unsigned int c = 0; c < kChannels; c++) {
                double sum = 0;
                for (double o : offsets) sum += expected(static_cast<uint64_t>(o * kSampleRate) + i, c) / 3.0;
                d = std::max(d, std::fabs(out[i * kChannels + c] - sum));
            }
        EXPECT(d < tolerance, "three voices differ from the expected mix by %g", d);
        soloud.stopAll();
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

bool copyFile(const std::string &from, const std::string &to) {
    FILE *in = std::fopen(from.c_str(), "rb");
    if (!in) return false;
    FILE *out = std::fopen(to.c_str(), "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }
    char buffer[65536];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) ok = ok && std::fwrite(buffer, 1, n, out) == n;
    std::fclose(in);
    return std::fclose(out) == 0 && ok;
}

/// Plays [voices] voices of [stream] at once, each at 1/[voices] gain, and
/// checks their sum against the full decode.
double mixVoicesAgainst(SoLoud::Soloud &soloud, SoLoud::NativeStream &stream, const FullDecode &full, int voices,
                        uint64_t frames) {
    std::vector<SoLoud::handle> handles;
    for (int i = 0; i < voices; i++) handles.push_back(playUnity(soloud, stream, 1.0f / voices));
    std::vector<float> out = mixPaced(soloud, handles, frames);
    double worst = 0;
    for (uint64_t i = 0; i < frames; i++)
        for (unsigned int c = 0; c < kChannels; c++)
            worst = std::max(worst, double(std::fabs(out[i * kChannels + c] -
                                                     full.interleaved[i * full.channels + (full.channels == 1 ? 0 : c)])));
    for (SoLoud::handle h : handles) soloud.stop(h);
    return worst;
}

void testPinnedFile(const std::string &path, const std::string &replacement, double tolerance) {
    std::printf("NativeStream: voices keep decoding the loaded file after its path is deleted and replaced\n");
    FullDecode full;
    if (!fullDecode(path, full) || full.rate != kSampleRate) {
        EXPECT(false, "needs a %u Hz full decode of %s", kSampleRate, path.c_str());
        return;
    }
    const char *tmpDir = std::getenv("TMPDIR");
    std::string pinned = std::string(tmpDir && *tmpDir ? tmpDir : "/tmp") + "/native_stream_pinned.m4a";
    EXPECT(copyFile(path, pinned), "copy to %s failed", pinned.c_str());

    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        EXPECT(stream.load(pinned.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
        // Cache eviction, then a different file under the same name.
        EXPECT(std::remove(pinned.c_str()) == 0, "delete failed");
        EXPECT(copyFile(replacement, pinned), "replace failed");

        // Two voices decode the pinned file at once.
        const double d = mixVoicesAgainst(soloud, stream, full, 2, std::min<uint64_t>(full.frames, 3 * kSampleRate));
        EXPECT(d < tolerance, "playback differs from the loaded file by %g", d);
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
    std::remove(pinned.c_str());
}

void testManyVoices(const std::string &path, int voices, double tolerance) {
    std::printf("NativeStream: %d voices of %s decode at once\n", voices, path.c_str());
    FullDecode full;
    if (!fullDecode(path, full) || full.rate != kSampleRate) {
        EXPECT(false, "needs a %u Hz full decode of %s", kSampleRate, path.c_str());
        return;
    }
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        EXPECT(stream.load(path.c_str()) == SoLoud::SO_NO_ERROR, "load failed");
        const uint64_t underruns = SoLoud::NativeStream::underrunFrameCount();
        const auto start = Clock::now();
        const double d = mixVoicesAgainst(soloud, stream, full, voices, std::min<uint64_t>(full.frames, 3 * kSampleRate));
        std::printf("  %.0f ms\n", msSince(start));
        EXPECT(d < tolerance, "%d voices differ from the full decode by %g", voices, d);
        EXPECT(SoLoud::NativeStream::underrunFrameCount() == underruns, "%d voices underran %llu frames", voices,
               static_cast<unsigned long long>(SoLoud::NativeStream::underrunFrameCount() - underruns));
    }
    soloud.deinit();
    EXPECT(waitForWorkersToExit(), "workers still running");
}

/// A file without an edit list or iTunSMPB: the stream is the container's
/// media duration, not the extractor's rounded duration.
void testNoGaplessMetadata(const std::string &path) {
    std::printf("backend: a file without gapless metadata streams its media duration\n");
    M4aGaplessInfo gapless;
    uint64_t delay = 0, valid = 0;
    EXPECT(parseM4aGapless(path, gapless) && !gapless.hasEdit && !gapless.hasSmpb, "%s has gapless metadata",
           path.c_str());
    std::string error;
    auto decoder = openPlatformNativeStreamDecoder(path, error);
    EXPECT(decoder != nullptr, "open failed: %s", error.c_str());
    if (!decoder) return;
    EXPECT(!gapless.resolve(decoder->format().sampleRate, delay, valid) && delay == 0 && valid > 0,
           "resolve should give the media duration");
#if defined(__ANDROID__)
    // Apple's decoder trims the AAC priming it assumes for such files itself.
    EXPECT(decoder->format().totalFrames == valid, "length %llu, media duration %llu",
           (unsigned long long)decoder->format().totalFrames, (unsigned long long)valid);
#endif
    std::vector<float> buf(4096 * decoder->format().channels);
    uint64_t total = 0;
    long got;
    while ((got = decoder->read(buf.data(), 4096)) > 0) total += static_cast<uint64_t>(got);
    EXPECT(got == 0 && total == decoder->format().totalFrames, "decoded %llu of %llu frames (last read %ld)",
           (unsigned long long)total, (unsigned long long)decoder->format().totalFrames, got);
}

void testAc3InMp4(const std::string &path) {
    std::printf("backend: AC-3 in MP4 trims the edit list delay\n");
    std::string error;
    auto decoder = openPlatformNativeStreamDecoder(path, error);
    if (!decoder) {
        std::printf("  skipped: no AC-3 decoder here (%s)\n", error.c_str());
        return;
    }
    M4aGaplessInfo gapless;
    uint64_t delay = 0, valid = 0;
    EXPECT(parseM4aGapless(path, gapless) && gapless.resolve(decoder->format().sampleRate, delay, valid),
           "no edit list in %s", path.c_str());
    EXPECT(decoder->format().totalFrames == valid, "length %llu, edit list says %llu",
           (unsigned long long)decoder->format().totalFrames, (unsigned long long)valid);

    // The full decode keeps the delay: stream frame f is full frame f + delay.
    DecodedAudioData full = NativeAudioDecoder::decodeFile(path.c_str());
    if (!full.success) {
        std::printf("  (no full decode to compare with: %s)\n", full.errorMessage.c_str());
        return;
    }
    const unsigned int channels = full.channels;
    std::vector<float> buf(4096 * channels);
    double worst = 0;
    for (uint64_t frame : {uint64_t(0), uint64_t(300000), uint64_t(100000)}) {
        decoder->seek(frame);
        long got = decoder->read(buf.data(), 4096);
        EXPECT(got == 4096, "read %ld frames", got);
        for (long i = 0; i < got && frame + delay + i < full.sampleCount; i++)
            worst = std::max(worst, double(std::fabs(buf[i * channels] - full.samples[frame + delay + i])));
    }
    full.freeSamples();
    EXPECT(worst < 1e-3, "PCM differs from the full decode shifted by %llu by %g", (unsigned long long)delay, worst);
}

#endif // NATIVE_STREAM_SUPPORTED

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress shows up through adb too
#if defined(__ANDROID__)
    // media.extractor reads our files through binder calls back into this
    // process. App processes always run a binder thread pool; a bare
    // executable has to start one, or after many extractors the callbacks
    // back up and AMediaExtractor_setDataSourceFd() hangs (seen on API 29).
    if (void *binder = dlopen("libbinder_ndk.so", RTLD_NOW)) {
        if (auto start = reinterpret_cast<void (*)()>(dlsym(binder, "ABinderProcess_startThreadPool"))) start();
    }
#endif
    if (argc > 1) gAssetDir = argv[1];
    if (argc > 2) gAppleEncoded = argv[2];
    if (argc > 3) gFfmpegEncoded = argv[3];
    if (argc > 4) gAc3InMp4 = argv[4];
    if (argc > 5) gNoEditList = argv[5];

    testGaplessParser();
    testSyntheticPlayback();
    testSyntheticSeek();
    testSyntheticLooping();
    testSyntheticLoopsBeyondHead();
    testSyntheticVoices();
    testSyntheticColdSeekWaits();
    testSyntheticUnderrun();
    testSyntheticRetroactiveRemix();
    testSyntheticHistoryBounds();
    testSyntheticTeardownWhileDecoding();
    testSyntheticWorkerFailure();
    testSyntheticFailureEndsLoopingVoice();
    testSyntheticWorkerFormatMismatch();
    testSyntheticIdleWorkerSleeps();
    testSyntheticPausedVoiceHoldsNoDecoder();
    testShortSoundHasNoWorker();
#if NATIVE_STREAM_SUPPORTED
    constexpr double kExact = 1e-6;
    constexpr double kNoiseSubstitution = 0.05;
    testBackendAgainstFullDecode(gAssetDir + "/aac_lc_stereo_48000.m4a", kExact);
    testBackendAgainstFullDecode(gAssetDir + "/aac_lc_mono_44100.m4a", kExact);
    testBackendAgainstFullDecode(gAssetDir + "/sample-AAC.m4a", kNoiseSubstitution);
    testStreamingMatchesFullDecode(gAssetDir + "/sample-AAC.m4a", kNoiseSubstitution);
    for (const std::string &file : {gAppleEncoded, gFfmpegEncoded}) {
        if (file.empty()) continue;
        testBackendAgainstFullDecode(file, kExact);
        testStreamingMatchesFullDecode(file, kExact);
    }
    if (!gAc3InMp4.empty()) testAc3InMp4(gAc3InMp4);
    if (!gNoEditList.empty()) testNoGaplessMetadata(gNoEditList);
    testPinnedFile(gAssetDir + "/sample-AAC.m4a", gAssetDir + "/aac_lc_mono_44100.m4a", kNoiseSubstitution);
    testManyVoices(gAssetDir + "/sample-AAC.m4a", 16, kNoiseSubstitution);
#else
    std::printf("(no streaming backend on this platform: backend tests skipped)\n");
#endif

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

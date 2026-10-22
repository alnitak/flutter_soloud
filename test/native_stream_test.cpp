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
    // When closed, read() blocks until it is opened again.
    std::mutex gateMutex;
    std::condition_variable gateCv;
    bool gateOpen = true;
    // Fail every open after the first (the load) to test a missing file.
    bool failWorkerOpen = false;

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
        mFormat.channels = 2;
        mFormat.sampleRate = static_cast<float>(kSampleRate);
        mFormat.totalFrames = totalFrames;
        mControl.opened++;
    }
    ~SyntheticDecoder() override { mControl.destroyed++; }

    const NativeStreamFormat &format() const override { return mFormat; }

    bool seek(uint64_t frame) override {
        mPosition = frame < mFormat.totalFrames ? frame : mFormat.totalFrames;
        return true;
    }

    long read(float *out, unsigned int maxFrames) override {
        mControl.inRead++;
        {
            std::unique_lock<std::mutex> lock(mControl.gateMutex);
            mControl.gateCv.wait(lock, [&] { return mControl.gateOpen; });
        }
        const int delay = mControl.readDelayUs.load();
        if (delay > 0) std::this_thread::sleep_for(std::chrono::microseconds(delay));
        mControl.inRead--;
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
    return [&control, totalFrames](const std::string &, std::string &error) -> std::unique_ptr<NativeStreamDecoder> {
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

/// Plays [source] at unity gain on both output channels (no pan law, no
/// start-up volume ramp), so mixer output equals the source PCM.
SoLoud::handle playUnity(SoLoud::Soloud &soloud, SoLoud::AudioSource &source, float volume = 1.0f) {
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

void testSyntheticUnderrun() {
    std::printf("synthetic: a slow decoder underruns into silence without blocking or drifting\n");
    SyntheticControl control;
    const uint64_t total = 4 * kSampleRate;
    SoLoud::Soloud soloud;
    if (!initEngine(soloud)) return;
    {
        SoLoud::NativeStream stream;
        stream.load("synthetic", syntheticFactory(control, total));
        // ~2048 frames per 200 ms: far slower than real time.
        control.readDelayUs = 200000;
        const uint64_t underrunsBefore = SoLoud::NativeStream::underrunFrameCount();
        SoLoud::handle h = playUnity(soloud, stream);
        soloud.seek(h, 2.0);

        const auto start = Clock::now();
        std::vector<float> out = mixPaced(soloud, {h}, 8 * kBufferSize, 0);
        EXPECT(msSince(start) < 100, "mixing must not wait for the decoder (took %.1f ms)", msSince(start));
        EXPECT(SoLoud::NativeStream::underrunFrameCount() > underrunsBefore, "expected underrun frames");
        // Whatever arrived is the right frame for its position: silence or
        // exactly frame 2 s + i.
        bool inSync = true;
        for (size_t i = 0; i < 8 * kBufferSize; i++) {
            const long long f = frameOf(&out[i * kChannels]);
            if (f != -1 && f != static_cast<long long>(2 * kSampleRate + i)) inSync = false;
        }
        EXPECT(inSync, "underrun output must be silence or the frame for that position");
        EXPECT(std::fabs(soloud.getStreamPosition(h) - (2.0 + 8.0 * kBufferSize / kSampleRate)) < 1e-6,
               "position kept moving: %f", soloud.getStreamPosition(h));

        // Once the decoder is fast again, playback recovers in sync.
        control.readDelayUs = 0;
        EXPECT(waitFor([&] { return buffered(soloud, h) >= kSampleRate / 4; }, 3000), "decoder should catch up");
        out = mixPaced(soloud, {h}, 3 * kBufferSize);
        expectFrames(out, 0, 2 * kSampleRate + 8 * kBufferSize, 3 * kBufferSize, "after recovery");
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
        EXPECT(waitFor([&] { return control.inRead.load() == 3; }, 2000), "workers should be decoding (%d)",
               control.inRead.load());
        mixPaced(soloud, voices, kSampleRate / 2, 0); // head cache still plays

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

    testGaplessParser();
    testSyntheticPlayback();
    testSyntheticSeek();
    testSyntheticLooping();
    testSyntheticVoices();
    testSyntheticUnderrun();
    testSyntheticTeardownWhileDecoding();
    testSyntheticWorkerFailure();
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
#else
    std::printf("(no streaming backend on this platform: backend tests skipped)\n");
#endif

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

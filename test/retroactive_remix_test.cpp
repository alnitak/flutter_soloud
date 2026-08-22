// Standalone native tests for retroactive re-mixing (Phase 3a of
// OPTION_B_RETROACTIVE_REMIX_PLAN.md): a play()/playScheduled() whose
// effective time falls inside the rendered-but-unplayed ring window rolls the
// engine back to the checkpoint at or before the event, replays the in-window
// journal, and re-mixes forward, so the new voice becomes audible at the
// playhead instead of at the next mix quantum.
//
// Null backend, no audio hardware: the test drives the device callback's
// duties (renderRingTopUp_internal + RenderRing::read) by hand. Ring frame
// positions are absolute (monotonic frame counters); "readPos" is the
// playhead frame.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_retroactive_remix_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_wav.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kQuantum = 1024;      // engine mix quantum (bufferSize)
constexpr unsigned int kRenderAhead = 4096;  // render-ahead depth in frames
constexpr unsigned int kDevicePeriod = 512;  // small device period in frames

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

// ---- Test audio sources ---------------------------------------------------

// Pulse sources are restorable: their whole consumption state is the frame
// offset. Emits `level` for the first `pulseFrames` source frames, silence
// after. When `finite`, the voice ends once the pulse is exhausted.
class PulseSnapshot final : public SoLoud::SourceStateSnapshot {
public:
    unsigned int offset = 0;
};

class PulseInstance final : public SoLoud::AudioSourceInstance {
public:
    PulseInstance(unsigned int pulseFrames, float level, bool finite)
        : pulseFrames_(pulseFrames), level_(level), finite_(finite) {}

    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int ch = 0; ch < mChannels; ++ch) {
            for (unsigned int i = 0; i < samplesToRead; ++i) {
                const unsigned int srcFrame = offset_ + i;
                buffer[ch * bufferSize + i] =
                    srcFrame < pulseFrames_ ? level_ : 0.0f;
            }
        }
        offset_ += samplesToRead;
        return samplesToRead;
    }

    bool hasEnded() override { return finite_ && offset_ >= pulseFrames_; }

    SoLoud::result rewind() override {
        offset_ = 0;
        mStreamPosition = 0;
        return SoLoud::SO_NO_ERROR;
    }

    SoLoud::SourceStateSnapshot* captureSourceState() override {
        auto* s = new PulseSnapshot();
        s->offset = offset_;
        return s;
    }

    void restoreSourceState(SoLoud::SourceStateSnapshot* state) override {
        offset_ = static_cast<PulseSnapshot*>(state)->offset;
    }

private:
    unsigned int pulseFrames_;
    float level_;
    bool finite_;
    unsigned int offset_ = 0;
};

class PulseSource final : public SoLoud::AudioSource {
public:
    PulseSource(unsigned int pulseFrames, float level, bool finite = false)
        : pulseFrames_(pulseFrames), level_(level), finite_(finite) {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = 1;
    }

    SoLoud::AudioSourceInstance* createInstance() override {
        return new PulseInstance(pulseFrames_, level_, finite_);
    }

private:
    unsigned int pulseFrames_;
    float level_;
    bool finite_;
};

// Constant-level source that never ends and is deliberately NOT restorable
// (default captureSourceState() == nullptr): makes every checkpoint
// unrestorable, so retroactive events must degrade to going-forward.
class NonRestorableDcInstance final : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int ch = 0; ch < mChannels; ++ch)
            std::fill(buffer + ch * bufferSize,
                      buffer + ch * bufferSize + samplesToRead, 0.25f);
        return samplesToRead;
    }
    bool hasEnded() override { return false; }
    SoLoud::result rewind() override { return SoLoud::SO_NO_ERROR; }
};

class NonRestorableDcSource final : public SoLoud::AudioSource {
public:
    NonRestorableDcSource() {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = 1;
    }
    SoLoud::AudioSourceInstance* createInstance() override {
        return new NonRestorableDcInstance();
    }
};

// ---- Rig ------------------------------------------------------------------

struct Rig {
    Rig() {
        engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
        const SoLoud::result result =
            engine.init(SoLoud::Soloud::CLIP_ROUNDOFF,
                        SoLoud::Soloud::NULLDRIVER, kSampleRate, kQuantum, 2);
        EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);
        EXPECT(engine.isRenderAheadEnabled(), "ring must be enabled");
    }

    ~Rig() { engine.deinit(); }

    void topUp() { engine.renderRingTopUp_internal(); }

    unsigned int available() { return engine.mRenderRing.availableToRead(); }

    unsigned long long readPos() {
        return engine.mRenderRing.getReadPosition();
    }

    void discard(unsigned int frames) {
        std::vector<float> tmp(frames * 2);
        engine.mRenderRing.read(tmp.data(), frames);
    }

    // Consume the whole unread window into a fresh buffer.
    std::vector<float> readAll() {
        const unsigned int n = available();
        std::vector<float> buf(n * 2, 0.0f);
        engine.mRenderRing.read(buf.data(), n);
        return buf;
    }

    SoLoud::Soloud engine;
};

// First frame whose left channel exceeds the threshold, or -1.
long long firstAudibleFrame(const std::vector<float>& buf, float threshold) {
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    for (unsigned int i = 0; i < frames; ++i) {
        if (std::fabs(buf[i * 2]) > threshold)
            return i;
    }
    return -1;
}

bool allSilent(const std::vector<float>& buf, unsigned int fromFrame,
               float threshold) {
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    for (unsigned int i = fromFrame; i < frames; ++i) {
        if (std::fabs(buf[i * 2]) > threshold ||
            std::fabs(buf[i * 2 + 1]) > threshold)
            return false;
    }
    return true;
}

// ---- Tests ----------------------------------------------------------------

// A retroactive play() becomes audible at the playhead (the very next unread
// ring frame), not at the next quantum boundary.
void testRetroactivePlayInsertion()
{
    Rig rig;
    PulseSource a(64, 1.0f);

    rig.topUp();
    EXPECT(rig.available() == kRenderAhead, "ring prefilled: %u",
           rig.available());
    rig.discard(2048);
    const unsigned long long eventReadPos = rig.readPos();

    const SoLoud::handle h = rig.engine.play(a);
    EXPECT(h != 0, "play failed");
    // The re-mix must not move the ring heads.
    EXPECT(rig.available() == kRenderAhead - 2048,
           "ring heads moved by re-mix: %u", rig.available());
    EXPECT(rig.readPos() == eventReadPos, "read head moved by re-mix");

    std::vector<float> buf = rig.readAll();
    const long long onset = firstAudibleFrame(buf, 0.02f);
    EXPECT(onset >= 0, "voice never became audible");
    // Onset at the playhead frame (slack for resampler/rounding transients).
    EXPECT(onset <= 4, "onset at frame %lld, expected ~0", onset);
    // The pulse lasts 64 frames; after it (plus slack) the window is silent.
    if (onset >= 0)
        EXPECT(allSilent(buf, (unsigned int)onset + 64 + 8, 0.02f),
               "pulse longer than expected");
}

// A second retroactive play must not erase the first one's audio from the
// window: the journal replay re-applies the earlier birth on the rolled-back
// state.
void testChordJournalReplay()
{
    Rig rig;
    // A: long pulse (still sounding when B lands). B: short pulse.
    PulseSource a(4096, 0.5f);
    PulseSource b(64, 0.5f);

    rig.topUp();
    rig.discard(2048);
    rig.engine.play(a);          // onset at ring frame 2048
    rig.discard(1024);           // playhead advances to 3072
    rig.engine.play(b);          // onset at ring frame 3072

    std::vector<float> buf = rig.readAll();  // starts at ring frame 3072
    // A alone before... A's onset is behind the read head now; the read
    // window starts exactly at B's onset.
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    EXPECT(frames >= 1024, "window too small: %u", frames);

    // First 64 frames: A + B mixed -> double level.
    double sumEarly = 0;
    for (unsigned int i = 0; i < 64; ++i)
        sumEarly += std::fabs(buf[i * 2]);
    // Frames 128..1024: A alone (B's pulse exhausted). If the journal replay
    // lost A this region is silent.
    double sumA = 0;
    for (unsigned int i = 128; i < 1024 && i < frames; ++i)
        sumA += std::fabs(buf[i * 2]);
    EXPECT(sumEarly > sumA / 10, "B missing at its onset (early %f, A %f)",
           sumEarly, sumA);
    EXPECT(sumA > 1.0, "A lost after B's rollback (sumA %f)", sumA);
}

// playScheduled inside the window lands at the exact frame; a time at/behind
// the playhead falls back to legacy ASAP placement (the next quantum at the
// write head).
void testScheduledInWindow()
{
    Rig rig;
    PulseSource a(64, 1.0f);
    PulseSource b(64, 1.0f);

    rig.topUp();
    rig.discard(2048);

    // In-window: 1000 frames ahead of the playhead.
    const SoLoud::time playhead = rig.engine.getPlayheadTime();
    rig.engine.playScheduled(playhead + 1000.0 / kSampleRate, a);

    // Behind the playhead: legacy placement at the write head.
    rig.engine.playScheduled(playhead - 1.0, b);

    std::vector<float> buf = rig.readAll();
    const long long onsetA = firstAudibleFrame(buf, 0.02f);
    EXPECT(onsetA >= 996 && onsetA <= 1004,
           "scheduled onset at %lld, expected ~1000", onsetA);

    // After A's pulse ends (~1072) the window is silent: B was scheduled
    // behind the playhead and got legacy placement, so it is not in the
    // already-mixed window at all.
    bool midSilent = true;
    for (unsigned int i = 1072; i * 2 < buf.size(); ++i)
        midSilent = midSilent && std::fabs(buf[i * 2]) <= 0.02f;
    EXPECT(midSilent, "legacy-scheduled voice leaked into the window");

    // B starts with the next mixed quantum (the old write head).
    rig.topUp();
    std::vector<float> buf2 = rig.readAll();
    const long long onsetB = firstAudibleFrame(buf2, 0.02f);
    EXPECT(onsetB >= 0 && onsetB <= 4,
           "legacy-scheduled onset at %lld, expected ~0 of the next quantum",
           onsetB);
}

// With a non-restorable voice sounding in the window, every checkpoint is
// unrestorable and the retroactive play degrades to legacy placement without
// corrupting the window's existing content.
void testFallbackNonRestorable()
{
    Rig rig;
    NonRestorableDcSource dc;
    PulseSource b(64, 1.0f);

    rig.engine.play(dc);
    rig.topUp();
    rig.discard(2048);

    const unsigned int before = rig.available();
    rig.engine.play(b);
    // Heads unmoved (no re-mix happened: there was nothing restorable).
    EXPECT(rig.available() == before, "fallback moved ring heads");

    std::vector<float> buf = rig.readAll();
    // The DC voice is intact from the first unread frame, and B -- placed
    // with legacy going-forward semantics -- is not in the window at all.
    EXPECT(std::fabs(buf[0]) > 0.05f, "DC voice corrupted at window start");
    EXPECT(firstAudibleFrame(buf, 0.5f) < 0,
           "non-restorable fallback leaked into the window");

    // B starts with the next mixed quantum, on top of the still-sounding DC.
    rig.topUp();
    std::vector<float> buf2 = rig.readAll();
    const long long onsetB = firstAudibleFrame(buf2, 0.5f);
    EXPECT(onsetB >= 0 && onsetB <= 4,
           "fallback onset at %lld, expected ~0 of the next quantum", onsetB);
}

// A voice that ends inside the window must report its end exactly once.
// Because the deleted instance cannot be resurrected, a rollback crossing its
// death degrades to legacy placement (the new voice starts at the next
// quantum); the window's existing audio is then untouched by construction.
void testEndedReconciliation()
{
    Rig rig;
    static std::atomic<int> endedCount{0};
    endedCount = 0;
    rig.engine.setVoiceEndedCallback([](unsigned int*) { endedCount++; });

    PulseSource a(1500, 0.5f, /*finite=*/true);  // ends mid-quantum 2
    PulseSource b(64, 1.0f);

    rig.engine.play(a);
    rig.topUp();  // A ends during this fill; its ended event dispatches here.
    EXPECT(endedCount == 1, "A's end dispatched %d times before rollback",
           endedCount.load());

    rig.discard(1024);  // playhead at 1024: A's death (frame ~1500) is in-window
    rig.engine.play(b); // rollback crosses A's death -> degrades to legacy

    rig.topUp();
    rig.discard(rig.available());
    EXPECT(endedCount == 1,
           "A's end dispatched %d times (re-mix double-fired it)",
           endedCount.load());

    rig.engine.setVoiceEndedCallback(nullptr);
}

// Empty retroactive event: a rollback that inserts a silent (zero-volume,
// inaudible) voice must reproduce the window bit-identically.
void testEmptyRemixBitExact()
{
    Rig ref;   // reference engine: no retroactive event
    Rig rig;   // gets the empty retroactive play
    PulseSource dcA(8192, 0.5f);
    PulseSource dcB(8192, 0.5f);
    PulseSource silent(64, 0.0f);

    ref.engine.play(dcA);
    rig.engine.play(dcB);
    ref.topUp();
    rig.topUp();
    ref.discard(2048);
    rig.discard(2048);

    // Zero-volume voice: inaudible, contributes nothing, but still triggers
    // the full rollback + re-mix.
    rig.engine.play(silent, 0.0f);

    std::vector<float> refBuf = ref.readAll();
    std::vector<float> buf = rig.readAll();
    EXPECT(refBuf.size() == buf.size(), "window size differs");
    EXPECT(refBuf == buf, "empty re-mix diverged from reference mix");
}

// Feature off: play() behaves exactly as before (voice starts at the next
// mix quantum; no ring, no journal, no checkpoints).
void testFeatureOffUnchanged()
{
    SoLoud::Soloud engine;
    const SoLoud::result result =
        engine.init(SoLoud::Soloud::CLIP_ROUNDOFF, SoLoud::Soloud::NULLDRIVER,
                    kSampleRate, kQuantum, 2);
    EXPECT(result == SoLoud::SO_NO_ERROR, "init failed: %d", result);
    EXPECT(!engine.isRenderAheadEnabled(), "ring must stay disabled");
    EXPECT(!engine.mRenderRing.isInited(), "ring must not be allocated");

    PulseSource a(64, 1.0f);
    engine.play(a);
    std::vector<float> out(kQuantum * 2, 0.0f);
    engine.mix(out.data(), kQuantum);
    // Legacy: the very first mixed quantum already contains the voice.
    const long long onset = firstAudibleFrame(out, 0.02f);
    EXPECT(onset >= 0 && onset <= 4, "legacy onset at %lld", onset);
    engine.deinit();
}

// A real in-memory Wav (the restorable built-in source): retroactive play
// must be accepted and land at the playhead.
void testWavRetroactive()
{
    Rig rig;
    // 8192 mono frames: 64-frame full-scale pulse at the start, then silence.
    std::vector<short> pcm(8192, 0);
    for (int i = 0; i < 64; ++i)
        pcm[i] = 32767;
    SoLoud::Wav wav;
    const SoLoud::result load = wav.loadRawWave16(
        pcm.data(), (unsigned int)pcm.size(), (float)kSampleRate, 1);
    EXPECT(load == SoLoud::SO_NO_ERROR, "loadRawWave16 failed: %d", load);

    rig.topUp();
    rig.discard(2048);
    const SoLoud::handle h = rig.engine.play(wav);
    EXPECT(h != 0, "play failed");

    std::vector<float> buf = rig.readAll();
    const long long onset = firstAudibleFrame(buf, 0.02f);
    EXPECT(onset >= 0 && onset <= 4,
           "wav retroactive onset at %lld, expected ~0", onset);
}

// A retroactive stop() silences the voice from the playhead on, with the
// ended callback firing exactly once.
void testRetroactiveStop()
{
    Rig rig;
    static std::atomic<int> endedCount{0};
    endedCount = 0;
    rig.engine.setVoiceEndedCallback([](unsigned int*) { endedCount++; });

    PulseSource a(8192, 0.5f);  // long pulse covering the whole window
    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(2048);

    rig.engine.stop(h);

    std::vector<float> buf = rig.readAll();
    // The stop lands at the playhead = frame 0 of the unread window.
    EXPECT(allSilent(buf, 4, 0.02f), "retroactive stop left audio in window");
    rig.topUp();
    rig.discard(rig.available());
    EXPECT(endedCount == 1, "ended fired %d times for retroactive stop",
           endedCount.load());
    rig.engine.setVoiceEndedCallback(nullptr);
}

// A retroactive setVolume() takes effect at (quantum-accurately around) the
// playhead instead of at the next mix quantum.
void testRetroactiveVolume()
{
    Rig rig;
    PulseSource a(8192, 1.0f);
    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(2500);  // playhead mid-quantum (boundaries at 2048/3072)

    rig.engine.setVolume(h, 0.25f);

    std::vector<float> buf = rig.readAll();
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    EXPECT(frames > 1200, "window too small: %u", frames);
    // Before the fader tick: original level. After the quantum containing the
    // playhead (frame 3072-2500=572 in window coords): reduced level.
    double early = 0, late = 0;
    for (unsigned int i = 0; i < 100; ++i)
        early += std::fabs(buf[i * 2]);
    for (unsigned int i = 700; i < 800 && i < frames; ++i)
        late += std::fabs(buf[i * 2]);
    EXPECT(late < early * 0.5,
           "volume change not audible in window (early %f, late %f)", early,
           late);
    // Roughly the requested ratio (the exact level passes through panning and
    // the post-clip scaler, so compare ratios, not absolutes).
    EXPECT(late > early * 0.1, "late level implausibly low (%f vs %f)", late,
           early);
}

// A retroactive setVolume() must survive a later rollback: the parameter
// journal re-applies it when a subsequent retroactive play crosses its time.
// A reference engine without the volume change provides the full-level
// baseline.
void testParamJournalReplay()
{
    Rig ref;
    PulseSource dcRef(8192, 1.0f);
    ref.engine.play(dcRef);
    ref.topUp();
    ref.discard(2560);
    std::vector<float> refBuf = ref.readAll();

    Rig rig;
    PulseSource a(8192, 1.0f);
    PulseSource b(64, 0.5f);

    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(2048);
    rig.engine.setVolume(h, 0.25f);  // retroactive, at playhead (frame 2048)
    rig.discard(512);                // playhead at 2560
    rig.engine.play(b);              // rollback crosses the volume change

    std::vector<float> buf = rig.readAll();
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    EXPECT(frames >= 1100 && refBuf.size() == buf.size(),
           "window size mismatch (%u vs %llu)", frames,
           (unsigned long long)refBuf.size());

    // A alone, well past the quantum containing the event (the change ramps
    // across one quantum): frames [700,800) in both windows.
    double rigA = 0, refA = 0;
    for (unsigned int i = 700; i < 800; ++i) {
        rigA += std::fabs(buf[i * 2]);
        refA += std::fabs(refBuf[i * 2]);
    }
    // The reduced level must be ~1/4 of the reference; if the param journal
    // were lost, it would equal the reference instead.
    EXPECT(rigA > refA * 0.1 && rigA < refA * 0.5,
           "A level after rollback: %f (reference %f, want ~0.25x)", rigA,
           refA);
    // B (0.5 pulse at the window start) adds on top of the reduced A.
    double duringB = 0;
    for (unsigned int i = 4; i < 60; ++i)
        duringB += std::fabs(buf[i * 2]);
    EXPECT(duringB / 56 > (rigA / 100) * 1.3,
           "B not audible on top of A (during %f, A %f)", duringB / 56,
           rigA / 100);
}

// A retroactive setPause() silences the voice around the playhead: the pause
// scheduler ticks at quantum boundaries, so it can take effect up to one
// quantum early (still far ahead of the legacy next-write-head behavior).
void testRetroactivePause()
{
    Rig rig;
    PulseSource a(8192, 1.0f);
    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(2500);  // playhead mid-quantum

    rig.engine.setPause(h, true);

    std::vector<float> buf = rig.readAll();
    // Silent from at most one quantum (1024 frames) after the playhead.
    EXPECT(allSilent(buf, 1032, 0.02f),
           "retroactive pause left audio after one quantum");
    // Legacy control: with the ring disabled the pause lands at the next mix
    // quantum (the write head), leaving the whole window sounding.
}

// An in-window scheduleStopAt stops the voice at exactly the scheduled time,
// not at the write head.
void testScheduledStopInWindow()
{
    Rig rig;
    static std::atomic<int> endedCount{0};
    endedCount = 0;
    rig.engine.setVoiceEndedCallback([](unsigned int*) { endedCount++; });

    PulseSource a(8192, 0.5f);
    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(2048);  // playhead at 2048

    const SoLoud::time t = rig.engine.getPlayheadTime() + 1000.0 / kSampleRate;
    rig.engine.scheduleStopAt(h, t);

    std::vector<float> buf = rig.readAll();
    // Audible until ~1000 frames into the window, silent after.
    EXPECT(!allSilent(buf, 0, 0.02f), "voice silent before the stop time");
    EXPECT(allSilent(buf, 1000 + 8, 0.02f),
           "voice audible past the scheduled stop");
    rig.topUp();
    rig.discard(rig.available());
    EXPECT(endedCount == 1, "ended fired %d times for scheduled stop",
           endedCount.load());
    rig.engine.setVoiceEndedCallback(nullptr);
}

// An in-window scheduleFadeAt starts the fade at the scheduled time instead
// of at the write head. Fader ticks are per-quantum, so the fade smears by up
// to one quantum; assert on coarse level regions.
void testScheduledFadeInWindow()
{
    Rig rig;
    PulseSource a(8192, 1.0f);
    const SoLoud::handle h = rig.engine.play(a);
    rig.topUp();
    rig.discard(1024);  // playhead at 1024; window is 3072 frames

    // Fade to 0 over 500 frames, starting 500 frames ahead of the playhead.
    const SoLoud::time t = rig.engine.getPlayheadTime() + 500.0 / kSampleRate;
    rig.engine.scheduleFadeAt(h, t, 0.0f, 500.0 / kSampleRate, false);

    std::vector<float> buf = rig.readAll();
    const unsigned int frames = (unsigned int)(buf.size() / 2);
    EXPECT(frames > 2100, "window too small: %u", frames);
    double start = 0, mid = 0;
    for (unsigned int i = 0; i < 50; ++i)
        start += std::fabs(buf[i * 2]);
    for (unsigned int i = 1500; i < 1600 && i < frames; ++i)
        mid += std::fabs(buf[i * 2]);
    // Legacy behavior (fade from the write head) would leave the whole window
    // at full level; the retroactive fade must be well into its ramp by
    // frame 1500 and done before frame 2100.
    EXPECT(mid < start * 0.7, "fade not started in window (%f vs %f)", mid,
           start);
    EXPECT(allSilent(buf, 2100, 0.02f), "fade not finished in window");
}

} // namespace

int main()
{
    testRetroactivePlayInsertion();
    testChordJournalReplay();
    testScheduledInWindow();
    testFallbackNonRestorable();
    testEndedReconciliation();
    testEmptyRemixBitExact();
    testFeatureOffUnchanged();
    testWavRetroactive();
    testRetroactiveStop();
    testRetroactiveVolume();
    testParamJournalReplay();
    testRetroactivePause();
    testScheduledStopInWindow();
    testScheduledFadeInWindow();

    std::fprintf(stderr, "\n%d assertions, %d failures\n", gAssertions,
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}

// Standalone C++ unit tests for RealtimeRingBuffer, RealtimeStream (alone and
// driven by a null-backend engine with the render-ahead ring),
// CustomAudioSource, CustomFilter, and uint64_t FilterInstance::mParamChanged.
//
// Build & run from the flutter_soloud repo root:
//
//   ./test/run_realtime_and_custom_dsp_test.sh

#include "../src/audiobuffer/realtime_ring_buffer.h"
#include "../src/audiobuffer/realtime_stream.h"
#include "../src/filters/custom_filter.h"
#include "../src/synth/custom_source.h"
#include "../src/soloud/include/soloud_biquadresonantfilter.h"
#include "../src/soloud/include/soloud_echofilter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

static int g_failures = 0;
static int g_assertions = 0;

#define EXPECT(cond, fmt, ...) do { \
    g_assertions++; \
    if (!(cond)) { \
        g_failures++; \
        std::fprintf(stderr, "  FAIL [%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

// 1. Test RealtimeRingBuffer SPSC planar & interleaved push/pop + SIMD de-interleaving + rewind
static void testRealtimeRingBuffer() {
    std::printf("Test: RealtimeRingBuffer (planar, interleaved float32/int16, wrap, rewind)\n");

    SoLoud::RealtimeRingBuffer rb;
    // init(channels=2, minCapacityFrames=256, historyFrames=64) -> power-of-2 cap=512, usable=448
    rb.init(2, 256, 64);
    const size_t usable = rb.capacityFrames();
    EXPECT(rb.availableToRead() == 0, "initial availableToRead != 0");
    EXPECT(rb.availableToWrite() == usable, "initial availableToWrite != %zu (%zu)",
           usable, rb.availableToWrite());

    // Push 100 stereo interleaved float32 frames (exercises SIMD stereo path)
    std::vector<float> inStereo(200);
    for (unsigned int i = 0; i < 100; ++i) {
        inStereo[i * 2]     = (float)i * 0.01f;
        inStereo[i * 2 + 1] = -(float)i * 0.01f;
    }
    size_t written = rb.pushInterleavedFloat32(inStereo.data(), 100);
    EXPECT(written == 100, "written != 100 (%zu)", written);
    EXPECT(rb.availableToRead() == 100, "availableToRead != 100 (%zu)", rb.availableToRead());
    EXPECT(rb.availableToWrite() == usable - 100, "availableToWrite != %zu (%zu)",
           usable - 100, rb.availableToWrite());

    // Pop 60 frames into planar output with stride 64
    std::vector<float> outPlanar(2 * 64, 0.0f);
    size_t popped = rb.popPlanar(outPlanar.data(), 60, 64);
    EXPECT(popped == 60, "popped != 60 (%zu)", popped);
    EXPECT(rb.availableToRead() == 40, "availableToRead after pop != 40 (%zu)", rb.availableToRead());

    bool match = true;
    for (unsigned int i = 0; i < 60; ++i) {
        if (std::fabs(outPlanar[i] - (float)i * 0.01f) > 1e-6f ||
            std::fabs(outPlanar[64 + i] - (-(float)i * 0.01f)) > 1e-6f) {
            match = false;
        }
    }
    EXPECT(match, "stereo de-interleaved samples mismatch");

    // Rewind 30 frames via setReadFrame (within the 64-frame history guard) and re-read
    const uint64_t currentRead = rb.getReadFrame();
    rb.setReadFrame(currentRead - 30);
    EXPECT(rb.availableToRead() == 70, "availableToRead after rewind != 70 (%zu)", rb.availableToRead());
    std::fill(outPlanar.begin(), outPlanar.end(), 0.0f);
    popped = rb.popPlanar(outPlanar.data(), 30, 64);
    EXPECT(popped == 30, "popped after rewind != 30 (%zu)", popped);
    bool rewindMatch = true;
    for (unsigned int i = 0; i < 30; ++i) {
        unsigned int origIdx = 30 + i;
        if (std::fabs(outPlanar[i] - (float)origIdx * 0.01f) > 1e-6f ||
            std::fabs(outPlanar[64 + i] - (-(float)origIdx * 0.01f)) > 1e-6f) {
            rewindMatch = false;
        }
    }
    EXPECT(rewindMatch, "rewound samples mismatch");

    // Drain remaining 40 frames
    rb.popPlanar(outPlanar.data(), 40, 64);
    EXPECT(rb.availableToRead() == 0, "ring not empty after drain");

    // Test wraparound and int16 interleaved conversion
    std::vector<int16_t> inInt16(440 * 2);
    for (unsigned int i = 0; i < 440; ++i) {
        inInt16[i * 2]     = 16384;  // +0.5
        inInt16[i * 2 + 1] = -16384; // -0.5
    }
    written = rb.pushInterleavedInt16(inInt16.data(), 440);
    EXPECT(written == 440, "int16 wrap push != 440 (%zu)", written);

    std::vector<float> outWrap(2 * 440, 0.0f);
    popped = rb.popPlanar(outWrap.data(), 440, 440);
    EXPECT(popped == 440, "int16 wrap pop != 440 (%zu)", popped);
    bool int16Match = true;
    for (unsigned int i = 0; i < 440; ++i) {
        if (std::fabs(outWrap[i] - 0.5f) > 1e-4f ||
            std::fabs(outWrap[440 + i] + 0.5f) > 1e-4f) {
            int16Match = false;
        }
    }
    EXPECT(int16Match, "int16 wrap conversion mismatch");

    // Test SIMD + scalar tail for interleaveStereoFloats, deinterleaveStereoInt16ToFloats, and convertInt16ToFloats (19 frames)
    constexpr size_t kOddFrames = 19;
    std::vector<int16_t> oddInt16(kOddFrames * 2);
    for (size_t i = 0; i < kOddFrames; ++i) {
        oddInt16[i * 2]     = static_cast<int16_t>((int)i * 500 - 4000);
        oddInt16[i * 2 + 1] = static_cast<int16_t>(-(int)i * 700 + 3000);
    }
    std::vector<float> oddL(kOddFrames, 0.0f), oddR(kOddFrames, 0.0f);
    SoLoud::deinterleaveStereoInt16ToFloats(oddInt16.data(), oddL.data(), oddR.data(), kOddFrames);
    std::vector<float> reInterleaved(kOddFrames * 2, 0.0f);
    SoLoud::interleaveStereoFloats(oddL.data(), oddR.data(), reInterleaved.data(), kOddFrames);
    std::vector<float> monoConverted(kOddFrames * 2, 0.0f);
    SoLoud::convertInt16ToFloats(oddInt16.data(), monoConverted.data(), kOddFrames * 2);

    bool simdTailMatch = true;
    for (size_t i = 0; i < kOddFrames * 2; ++i) {
        const float expected = static_cast<float>(oddInt16[i]) * (1.0f / 32768.0f);
        if (std::fabs(reInterleaved[i] - expected) > 1e-6f ||
            std::fabs(monoConverted[i] - expected) > 1e-6f) {
            simdTailMatch = false;
        }
    }
    EXPECT(simdTailMatch, "SIMD + scalar tail mismatch for int16/stereo helpers");
}

// 2. Test RealtimeStream & RealtimeStreamInstance (underrun, capture/restore state, cosine anti-click ramp)
static void testRealtimeStream() {
    std::printf("Test: RealtimeStream (zero-wait pull, state capture/restore, cosine anti-click)\n");

    SoLoud::RealtimeStream stream;
    PlayerErrors err = stream.init(nullptr, 48000, 2,
                                   BufferType::PCM_F32LE_PLANAR, 1024);
    EXPECT(err == PlayerErrors::noError, "RealtimeStream::init failed (%d)", (int)err);

    SoLoud::AudioSourceInstance *baseInst = stream.createInstance();
    auto *inst = static_cast<SoLoud::RealtimeStreamInstance *>(baseInst);

    // Push 200 planar frames first so the stream becomes active (not underrun)
    std::vector<float> planarIn(2 * 200);
    for (unsigned int i = 0; i < 200; ++i) {
        planarIn[i]       = 0.25f;
        planarIn[200 + i] = 0.75f;
    }
    unsigned int pushed = stream.pushDataFloat32(planarIn.data(), 200);
    EXPECT(pushed == 200, "pushDataFloat32 != 200 (%u)", pushed);

    // Capture source state before pulling 100 frames (simulating retroactive mixer snapshot)
    SoLoud::SourceStateSnapshot *snap = inst->captureSourceState();
    std::vector<float> buf(2 * 128, 0.0f);
    inst->getAudio(buf.data(), 100, 128);
    EXPECT(stream.getAvailableToRead() == 100, "available after 100 pull != 100 (%u)",
           stream.getAvailableToRead());
    EXPECT(std::fabs(buf[0] - 0.25f) < 1e-6f && std::fabs(buf[128] - 0.75f) < 1e-6f,
           "pulled planar values mismatch");

    // Restore source state -> should rewind the 100 frames in the ring buffer!
    inst->restoreSourceState(snap);
    delete snap;
    EXPECT(stream.getAvailableToRead() == 200, "available after restoreSourceState != 200 (%u)",
           stream.getAvailableToRead());

    // Drain all 200 frames and trigger underrun on the next pull
    inst->getAudio(buf.data(), 128, 128);
    inst->getAudio(buf.data(), 128, 128); // reads 72 frames, then underruns for 56 frames
    EXPECT(stream.getUnderrunCount() == 1, "underrunCount != 1 (%u)", stream.getUnderrunCount());
    EXPECT(!inst->hasEnded(), "RealtimeStream should never auto-end on underrun");

    delete inst;
}

// 3. Test CustomAudioSource (callbacks, lifecycle callback, stream end)
struct SynthState {
    float value = 0.5f;
    int createdEvents = 0;
    int destroyedEvents = 0;
    bool shouldEnd = false;
};

static SynthState g_synthState;

static void onSynthLifecycle(uint32_t eventType, uint32_t playIndex, void *userData) {
    auto *st = static_cast<SynthState *>(userData);
    if (eventType == 1) {
        st->createdEvents++;
    } else if (eventType == 0) {
        st->destroyedEvents++;
    }
}

static unsigned int onSynthGenerate(float *buffer, unsigned int samplesToRead,
                                    unsigned int bufferSize, unsigned int channels,
                                    float sampleRate, double streamTime,
                                    double streamPosition, uint32_t *outHasEnded,
                                    void *userData) {
    auto *st = static_cast<SynthState *>(userData);
    for (unsigned int ch = 0; ch < channels; ++ch) {
        float *dst = buffer + ch * bufferSize;
        for (unsigned int i = 0; i < samplesToRead; ++i) {
            dst[i] = st->value + (float)ch * 0.1f;
        }
    }
    *outHasEnded = st->shouldEnd ? 1u : 0u;
    return samplesToRead;
}

static void testCustomAudioSource() {
    std::printf("Test: CustomAudioSource (callback, lifecycle callback, stream termination)\n");

    g_synthState = SynthState{};
    SoLoud::CustomAudioSource src(48000, 2, onSynthGenerate, &g_synthState,
                                  onSynthLifecycle, false, nullptr);

    SoLoud::AudioSourceInstance *inst = src.createInstance();
    std::vector<float> buf(2 * 64, 0.0f);
    unsigned int read = inst->getAudio(buf.data(), 64, 64);
    EXPECT(g_synthState.createdEvents == 1, "lifecycle created event not fired");
    EXPECT(read == 64, "CustomAudioSource getAudio != 64");
    EXPECT(std::fabs(buf[0] - 0.5f) < 1e-6f && std::fabs(buf[64] - 0.6f) < 1e-6f,
           "CustomAudioSource output mismatch (%.3f, %.3f)", buf[0], buf[64]);
    EXPECT(!inst->hasEnded(), "CustomAudioSource should not end when *outHasEnded == 0");

    g_synthState.shouldEnd = true;
    inst->getAudio(buf.data(), 64, 64);
    EXPECT(inst->hasEnded(), "CustomAudioSource should end when *outHasEnded == 1");

    delete inst;
    EXPECT(g_synthState.destroyedEvents == 1, "lifecycle destroyed event not fired");

    // A re-mix restores a snapshot taken before the instance's first
    // getAudio(); that must not report the instance as created again.
    g_synthState = SynthState{};
    SoLoud::AudioSourceInstance *again = src.createInstance();
    SoLoud::SourceStateSnapshot *birth = again->captureSourceState();
    EXPECT(birth != nullptr && !birth->mComplete,
           "a callback-driven source snapshot must be marked incomplete");
    again->getAudio(buf.data(), 64, 64);
    again->restoreSourceState(birth);
    again->getAudio(buf.data(), 64, 64);
    delete birth;
    EXPECT(g_synthState.createdEvents == 1,
           "created event fired %d times across a state restore",
           g_synthState.createdEvents);
    delete again;
}

// 4. Test CustomFilter (3-branch wet behavior, zero-copy at wet=1, dry/wet blend, param definitions, uint64_t mParamChanged)
struct FilterState {
    int callbackCalls = 0;
    float *lastBufferPtr = nullptr;
    float lastParam1 = 0.0f;
    uint64_t lastParamChangedMask = 0;
};

static FilterState g_filterState;

static void onCustomFilterDsp(float *buffer, unsigned int samples,
                              unsigned int bufferSize, unsigned int channels,
                              float sampleRate, double time,
                              const float *params, uint64_t paramChangedMask,
                              unsigned int paramCount, void *userData) {
    auto *st = static_cast<FilterState *>(userData);
    st->callbackCalls++;
    st->lastBufferPtr = buffer;
    st->lastParam1 = paramCount > 1 ? params[1] : 0.0f;
    st->lastParamChangedMask = paramChangedMask;

    // Multiply planar signal by params[1] (gain)
    for (unsigned int ch = 0; ch < channels; ++ch) {
        float *dst = buffer + ch * bufferSize;
        for (unsigned int i = 0; i < samples; ++i) {
            dst[i] *= st->lastParam1;
        }
    }
}

static void testCustomFilter() {
    std::printf("Test: CustomFilter (wet=0 bypass, wet=1 zero-copy, 0<wet<1 blend, params, uint64_t mask)\n");

    g_filterState = FilterState{};
    SoLoud::CustomFilter filter;
    filter.setCallback(onCustomFilterDsp, &g_filterState, 2);
    filter.setParamDefinition(0, 1.0f, 0.0f, 1.0f, "Wet");
    filter.setParamDefinition(1, 2.0f, 0.0f, 4.0f, "Gain");

    EXPECT(filter.getParamCount() == 2, "getParamCount != 2 (%d)", filter.getParamCount());
    EXPECT(std::strcmp(filter.getParamName(1), "Gain") == 0, "getParamName(1) != Gain");
    EXPECT(filter.getParamMax(1) == 4.0f, "getParamMax(1) != 4.0f");
    EXPECT(filter.getParamDefault(1) == 2.0f, "getParamDefault(1) != 2.0f");

    SoLoud::FilterInstance *inst = filter.createInstance();
    std::vector<float> buf(2 * 64, 0.5f);

    // Branch 1: wet == 0.0 -> hard bypass (callback NOT called, buffer untouched)
    inst->setFilterParameter(0, 0.0f);
    inst->filter(buf.data(), 64, 64, 2, 48000.0f, 0.0);
    EXPECT(g_filterState.callbackCalls == 0, "callback should be bypassed when wet == 0.0");
    EXPECT(std::fabs(buf[0] - 0.5f) < 1e-6f, "buffer modified during bypass");

    // Branch 2: wet == 1.0 -> 100% wet zero-copy in-place (callback receives exact buf.data())
    inst->setFilterParameter(0, 1.0f);
    inst->setFilterParameter(1, 2.0f); // Gain = 2.0 -> bits 0 and 1 set in mParamChanged
    inst->filter(buf.data(), 64, 64, 2, 48000.0f, 0.0);
    EXPECT(g_filterState.callbackCalls == 1, "callback not called when wet == 1.0");
    EXPECT(g_filterState.lastBufferPtr == buf.data(), "wet == 1.0 should be zero-copy in-place");
    EXPECT((g_filterState.lastParamChangedMask & 0x3ULL) == 0x3ULL,
           "paramChangedMask bits 0 and 1 not set (0x%llx)",
           (unsigned long long)g_filterState.lastParamChangedMask);
    EXPECT(std::fabs(buf[0] - 1.0f) < 1e-6f, "wet == 1.0 output mismatch (%.3f != 1.0)", buf[0]);

    // Branch 3: 0.0 < wet < 1.0 -> automatic dry/wet blend
    // Input = 1.0, Gain = 3.0 -> processed = 3.0. With wet = 0.25:
    // output = 0.75 * 1.0 + 0.25 * 3.0 = 1.5
    std::fill(buf.begin(), buf.end(), 1.0f);
    inst->setFilterParameter(0, 0.25f);
    inst->setFilterParameter(1, 3.0f);
    inst->filter(buf.data(), 64, 64, 2, 48000.0f, 0.0);
    EXPECT(g_filterState.callbackCalls == 2, "callback not called when wet == 0.25");
    EXPECT(std::fabs(buf[0] - 1.5f) < 1e-6f && std::fabs(buf[127] - 1.5f) < 1e-6f,
           "dry/wet blend mismatch (%.3f != 1.5)", buf[0]);

    // Test FilterStateSnapshot capture/restore
    SoLoud::FilterStateSnapshot *snap = inst->captureState();
    inst->setFilterParameter(1, 0.5f);
    inst->restoreState(snap);
    delete snap;
    EXPECT(std::fabs(inst->getFilterParameter(1) - 3.0f) < 1e-6f,
           "restoreState did not restore param 1 (%.3f != 3.0)", inst->getFilterParameter(1));

    // Branch 3 with a block larger than the pre-allocated dry scratch (e.g. a
    // global filter on a large engine quantum): blended in scratch-sized
    // chunks instead of allocating on the audio thread. Every frame of both
    // channels must still be blended, and only the first chunk reports the
    // parameter change.
    {
        const unsigned int frames = 1500;
        std::vector<float> big(2 * frames, 1.0f);
        const int callsBefore = g_filterState.callbackCalls;
        inst->setFilterParameter(1, 3.0f);
        inst->filter(big.data(), frames, frames, 2, 48000.0f, 0.0);
        const int calls = g_filterState.callbackCalls - callsBefore;
        EXPECT(calls == 3, "large block should be blended in 3 chunks (%d)", calls);
        bool allBlended = true;
        for (float v : big) {
            if (std::fabs(v - 1.5f) > 1e-6f) {
                allBlended = false;
                break;
            }
        }
        EXPECT(allBlended, "chunked dry/wet blend mismatch");
        EXPECT(g_filterState.lastParamChangedMask == 0,
               "only the first chunk should report changed params (0x%llx)",
               (unsigned long long)g_filterState.lastParamChangedMask);
    }

    delete inst;

    SoLoud::CustomAudioSource namedSynth(48000, 2, nullptr, nullptr, nullptr,
                                         false, "soloudCustomSynthCallback");
    SoLoud::AudioSourceInstance *namedSynthInst = namedSynth.createInstance();
    std::fill(buf.begin(), buf.end(), 0.0f);
    unsigned int synthRead = namedSynthInst->getAudio(buf.data(), 64, 64);
    EXPECT(synthRead == 64 && (std::fabs(buf[10]) > 1e-5f || std::fabs(buf[74]) > 1e-5f),
           "soloudCustomSynthCallback did not synthesize audio");
    delete namedSynthInst;
}


// 5. RealtimeStream against a real engine (null backend): timed pushes,
// reset, and retroactive re-mixing of the render-ahead window.

namespace rt_engine {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kQuantum = 1024;
constexpr unsigned int kRenderAhead = 4096;
constexpr unsigned int kDevicePeriod = 512;
constexpr float kLevel = 0.4f;
constexpr float kThreshold = 0.02f;

// Left channel of an interleaved stereo buffer.
static float left(const std::vector<float> &buf, unsigned int frame) {
    return buf[frame * 2];
}

static long long firstAudible(const std::vector<float> &buf, unsigned int from) {
    const unsigned int frames = static_cast<unsigned int>(buf.size() / 2);
    for (unsigned int i = from; i < frames; ++i) {
        if (std::fabs(left(buf, i)) > kThreshold) {
            return i;
        }
    }
    return -1;
}

static bool silent(const std::vector<float> &buf, unsigned int from, unsigned int to) {
    to = std::min<unsigned int>(to, static_cast<unsigned int>(buf.size() / 2));
    for (unsigned int i = from; i < to; ++i) {
        if (std::fabs(left(buf, i)) > kThreshold) {
            return false;
        }
    }
    return true;
}

static bool audible(const std::vector<float> &buf, unsigned int from, unsigned int to) {
    to = std::min<unsigned int>(to, static_cast<unsigned int>(buf.size() / 2));
    for (unsigned int i = from; i < to; ++i) {
        if (std::fabs(left(buf, i)) <= kThreshold) {
            return false;
        }
    }
    return from < to;
}

static double frameTime(unsigned int frames) {
    return static_cast<double>(frames) / kSampleRate;
}

struct RingRig {
    RingRig() {
        engine.setRenderAheadConfig(kDevicePeriod, kRenderAhead);
        const SoLoud::result r = engine.init(SoLoud::Soloud::CLIP_ROUNDOFF,
                                             SoLoud::Soloud::NULLDRIVER,
                                             kSampleRate, kQuantum, 2);
        EXPECT(r == SoLoud::SO_NO_ERROR, "engine init failed (%d)", r);
    }
    ~RingRig() { engine.deinit(); }

    void topUp() { engine.renderRingTopUp_internal(); }
    void discard(unsigned int frames) {
        std::vector<float> tmp(frames * 2);
        engine.mRenderRing.read(tmp.data(), frames);
    }
    std::vector<float> readAll() {
        const unsigned int n = engine.mRenderRing.availableToRead();
        std::vector<float> buf(n * 2, 0.0f);
        engine.mRenderRing.read(buf.data(), n);
        return buf;
    }
    double playhead() { return engine.getPlayheadTime(); }

    SoLoud::Soloud engine;
};

// A chunk scheduled after the end of audio that is still queued must wait for
// its time, not play right after the queued audio.
static void testScheduledAfterQueuedAudio() {
    std::printf("Test: RealtimeStream scheduled push after queued audio keeps its time\n");

    SoLoud::Soloud engine;
    const SoLoud::result r = engine.init(SoLoud::Soloud::CLIP_ROUNDOFF,
                                         SoLoud::Soloud::NULLDRIVER,
                                         kSampleRate, kQuantum, 2);
    EXPECT(r == SoLoud::SO_NO_ERROR, "engine init failed (%d)", r);

    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&engine, kSampleRate, 1, BufferType::PCM_F32LE, 8192) ==
               PlayerErrors::noError,
           "init failed");
    EXPECT(engine.play(stream) != 0, "play failed");

    std::vector<float> chunk(100, kLevel);
    const double t0 = engine.getEngineTime();
    EXPECT(stream.pushDataFloat32(chunk.data(), 100) == 100, "push A failed");
    EXPECT(stream.pushDataScheduledFloat32(t0 + frameTime(1000), chunk.data(),
                                           100) == 100,
           "push B failed");

    std::vector<float> out(2 * kQuantum * 2, 0.0f);
    engine.mix(out.data(), kQuantum);
    engine.mix(out.data() + kQuantum * 2, kQuantum);

    EXPECT(audible(out, 4, 96), "A should play at once");
    EXPECT(silent(out, 100 + 70, 996), "gap before B should be silent");
    const long long onsetB = firstAudible(out, 170);
    EXPECT(onsetB >= 996 && onsetB <= 1004, "B onset at %lld, want 1000", onsetB);
    EXPECT(audible(out, 1004, 1096), "B should play for its length");

    engine.deinit();
}

// A chunk scheduled inside the render-ahead window, after audio that ends
// inside the same window, must not erase that earlier audio when the window
// is re-mixed.
static void testScheduledInWindowKeepsEarlierAudio() {
    std::printf("Test: RealtimeStream scheduled pushes inside the render-ahead window\n");

    RingRig rig;
    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                       16384) == PlayerErrors::noError,
           "init failed");
    EXPECT(rig.engine.play(stream) != 0, "play failed");

    rig.topUp();          // the starving voice fills the window with silence
    rig.discard(1024);    // playhead at ring frame 1024
    const double playhead = rig.playhead();

    std::vector<float> chunk(300, kLevel);
    EXPECT(stream.pushDataScheduledFloat32(playhead + frameTime(500),
                                           chunk.data(), 300) == 300,
           "push A failed");
    EXPECT(stream.pushDataScheduledFloat32(playhead + frameTime(1000),
                                           chunk.data(), 300) == 300,
           "push B failed");

    const std::vector<float> buf = rig.readAll();  // starts at the playhead
    const long long onsetA = firstAudible(buf, 0);
    EXPECT(onsetA >= 496 && onsetA <= 504, "A onset at %lld, want 500", onsetA);
    EXPECT(audible(buf, 504, 796), "A missing from the re-mixed window");
    EXPECT(silent(buf, 800 + 70, 996), "gap between A and B should be silent");
    const long long onsetB = firstAudible(buf, 870);
    EXPECT(onsetB >= 996 && onsetB <= 1004, "B onset at %lld, want 1000", onsetB);
    EXPECT(audible(buf, 1004, 1296), "B missing from the re-mixed window");
    EXPECT(silent(buf, 1300 + 70, static_cast<unsigned int>(buf.size() / 2)),
           "nothing should play after B");
}

// An immediate push to a starving voice is spliced in at the playhead.
static void testImmediatePushResumesAtPlayhead() {
    std::printf("Test: RealtimeStream immediate push to a starving voice\n");

    RingRig rig;
    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                       16384) == PlayerErrors::noError,
           "init failed");
    EXPECT(rig.engine.play(stream) != 0, "play failed");

    rig.topUp();
    rig.discard(1024);

    std::vector<float> chunk(2000, kLevel);
    EXPECT(stream.pushDataFloat32(chunk.data(), 2000) == 2000, "push failed");

    const std::vector<float> buf = rig.readAll();
    const long long onset = firstAudible(buf, 0);
    EXPECT(onset >= 0 && onset <= 4, "onset at %lld, want the playhead", onset);
    EXPECT(audible(buf, 4, 1996), "pushed audio should play contiguously");
    EXPECT(silent(buf, 2000 + 70, static_cast<unsigned int>(buf.size() / 2)),
           "nothing should play after the pushed audio");
}

// Re-mix the render-ahead window from the checkpoint at or before `when`, as
// any retroactive event would. Returns false if no re-mix was possible.
static bool remixFrom(RingRig &rig, double when) {
    rig.engine.lockAudioMutex_internal();
    const int ci = rig.engine.retroactiveBegin_internal(when);
    if (ci >= 0) {
        rig.engine.retroactiveEnd_internal(ci);
    }
    rig.engine.unlockAudioMutex_internal();
    return ci >= 0;
}

// Frames discarded by reset() must not come back when a later event re-mixes
// the window.
static void testResetSurvivesRemix() {
    std::printf("Test: RealtimeStream reset is not undone by a re-mix\n");

    RingRig rig;
    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                       16384) == PlayerErrors::noError,
           "init failed");
    EXPECT(rig.engine.play(stream) != 0, "play failed");

    std::vector<float> chunk(8000, kLevel);
    EXPECT(stream.pushDataFloat32(chunk.data(), 8000) == 8000, "push failed");
    rig.topUp();          // mixes stream frames [0, 4096)
    rig.discard(2048);    // playhead at 2048
    const double resetTime = rig.engine.getEngineTime();  // ring frame 4096

    stream.resetBuffer(); // discards stream frames [4096, 8000)
    EXPECT(stream.getAvailableToRead() == 0, "reset left %u frames",
           stream.getAvailableToRead());
    rig.topUp();          // mixes [4096, 6144): the stream is dry

    // The discarded frames freed their ring space, so a re-mix from before
    // the reset could replay overwritten history: it must be refused.
    EXPECT(!remixFrom(rig, rig.engine.getPlayheadTime()),
           "re-mix from before the reset should be refused");
    // A re-mix from the reset point is fine and must not replay them.
    EXPECT(remixFrom(rig, resetTime), "re-mix from the reset point failed");

    const std::vector<float> buf = rig.readAll();  // starts at ring frame 2048
    EXPECT(audible(buf, 0, 2040), "audio mixed before the reset should remain");
    EXPECT(silent(buf, 2048 + 70, static_cast<unsigned int>(buf.size() / 2)),
           "discarded frames replayed by the re-mix");
}

// A reset while the voice waits out a scheduled gap cancels the gap; a
// re-mix from a checkpoint taken during the gap must cancel it too.
static void testResetDuringScheduledGap() {
    std::printf("Test: RealtimeStream reset during a scheduled gap\n");

    RingRig rig;
    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                       16384) == PlayerErrors::noError,
           "init failed");
    EXPECT(rig.engine.play(stream) != 0, "play failed");

    rig.topUp();                // the starving voice mixes [0, 4096)
    rig.discard(1024);
    std::vector<float> chunk(300, kLevel);
    // Scheduled well past the mix head: the voice is still in the gap when
    // the next quantum [4096, 5120) is mixed.
    EXPECT(stream.pushDataScheduledFloat32(0.0 + frameTime(6000), chunk.data(),
                                           300) == 300,
           "scheduled push failed");
    rig.topUp();                // mixes [4096, 5120) inside the gap
    const double resetTime = rig.engine.getEngineTime();  // ring frame 5120

    stream.resetBuffer();       // drops the scheduled chunk and its gap
    std::vector<float> next(2000, kLevel);
    EXPECT(stream.pushDataFloat32(next.data(), 2000) == 2000, "push failed");
    rig.discard(2048);          // playhead at 3072
    rig.topUp();                // mixes [5120, 7168): the new frames

    EXPECT(remixFrom(rig, resetTime), "re-mix from the reset point failed");

    const std::vector<float> buf = rig.readAll();  // starts at ring frame 3072
    // Ring frame 5120 is window frame 2048.
    EXPECT(silent(buf, 0, 2044), "nothing should play before the reset point");
    const long long onset = firstAudible(buf, 0);
    EXPECT(onset >= 2048 && onset <= 2052,
           "new frames start at window frame %lld, want 2048", onset);
}

struct PeakProbe {
    float inputPeak = 0.0f;
    double firstTime = -1.0;
};

// Global filter that records the peak it receives, then boosts by 4x.
static void probeThenBoost(float *buffer, unsigned int samples,
                           unsigned int bufferSize, unsigned int channels,
                           float, double time, const float *, uint64_t,
                           unsigned int, void *userData) {
    auto *probe = static_cast<PeakProbe *>(userData);
    if (probe->firstTime < 0.0) {
        probe->firstTime = time;
    }
    for (unsigned int ch = 0; ch < channels; ++ch) {
        float *dst = buffer + ch * bufferSize;
        for (unsigned int i = 0; i < samples; ++i) {
            probe->inputPeak = std::max(probe->inputPeak, std::fabs(dst[i]));
            dst[i] *= 4.0f;
        }
    }
}

// With the render-ahead ring, global filters run post-ring at the playhead.
// They must still see the unclipped mix, and their output must still be
// clipped, as on the direct mix path.
static void testRenderAheadGlobalFilterThenClip() {
    std::printf("Test: render-ahead ring clips after the global filters\n");

    RingRig rig;
    PeakProbe probe;
    SoLoud::CustomFilter filter;
    filter.setCallback(probeThenBoost, &probe, 1);
    rig.engine.setGlobalFilter(0, &filter);

    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                       16384) == PlayerErrors::noError,
           "init failed");
    std::vector<float> loud(8192, 3.0f);  // well past the clipper's knee
    EXPECT(stream.pushDataFloat32(loud.data(), 8192) == 8192, "push failed");
    EXPECT(rig.engine.play(stream) != 0, "play failed");

    rig.topUp();
    rig.discard(1024);  // skip the voice's start-up quantum
    const double chunkStart = rig.engine.getPlayheadTime();
    std::vector<float> out(1024 * 2, 0.0f);
    EXPECT(rig.engine.mRenderRing.read(out.data(), 1024) == 1024, "ring read failed");
    rig.engine.postProcessRenderRing_internal(out.data(), 1024);
    EXPECT(std::fabs(probe.firstTime - chunkStart) < 1e-6,
           "global filter time %.6f, want the chunk start %.6f",
           probe.firstTime, chunkStart);

    float outPeak = 0.0f;
    for (float v : out) {
        outPeak = std::max(outPeak, std::fabs(v));
    }
    EXPECT(probe.inputPeak > 1.2f,
           "global filter saw an already clipped mix (peak %.3f)", probe.inputPeak);
    EXPECT(outPeak <= 1.0f, "output not clipped after the global filter (peak %.3f)",
           outPeak);
    EXPECT(outPeak > 0.9f, "output unexpectedly quiet (peak %.3f)", outPeak);

    rig.engine.setGlobalFilter(0, nullptr);
    rig.engine.stopAll();
}

// A per-voice custom filter with a user callback keeps state SoLoud cannot
// snapshot, so a re-mix would run it twice over the window: checkpoints
// covering such a voice must not be restorable. The same voice with the
// filter bypassed (no callback) stays restorable.
static void testRemixRefusedWithStatefulCustomDsp() {
    std::printf("Test: re-mix refused while stateful custom DSP plays\n");

    for (int withCallback = 0; withCallback < 2; ++withCallback) {
        RingRig rig;
        PeakProbe probe;
        SoLoud::CustomFilter filter;
        if (withCallback) {
            filter.setCallback(probeThenBoost, &probe, 1);
        }
        SoLoud::RealtimeStream stream;
        EXPECT(stream.init(&rig.engine, kSampleRate, 1, BufferType::PCM_F32LE,
                           16384) == PlayerErrors::noError,
               "init failed");
        stream.setFilter(0, &filter);
        std::vector<float> chunk(8000, 0.1f);
        stream.pushDataFloat32(chunk.data(), 8000);
        EXPECT(rig.engine.play(stream) != 0, "play failed");
        rig.topUp();
        rig.discard(1024);

        const bool remixed = remixFrom(rig, rig.engine.getPlayheadTime());
        if (withCallback) {
            EXPECT(!remixed, "re-mix must be refused with a stateful custom filter");
        } else {
            EXPECT(remixed, "re-mix should work with a bypassed custom filter");
        }
        rig.engine.stopAll();
        stream.setFilter(0, nullptr);
    }
}

// Shrinking the voice pool reallocates the resampler blocks that
// checkpoints refer to by index: none of the older checkpoints may be
// restored afterwards.
static void testMaxActiveVoiceShrinkRetiresCheckpoints() {
    std::printf("Test: shrinking max active voices retires checkpoints\n");

    RingRig rig;
    // Half the engine rate, so every voice resamples and owns pool blocks.
    constexpr unsigned int kVoices = 6;
    std::vector<std::unique_ptr<SoLoud::RealtimeStream>> streams;
    std::vector<float> chunk(8000, 0.05f);
    for (unsigned int i = 0; i < kVoices; ++i) {
        streams.emplace_back(new SoLoud::RealtimeStream());
        EXPECT(streams.back()->init(&rig.engine, kSampleRate / 2, 1,
                                    BufferType::PCM_F32LE, 16384) ==
                   PlayerErrors::noError,
               "init failed");
        streams.back()->pushDataFloat32(chunk.data(), 8000);
        EXPECT(rig.engine.play(*streams.back()) != 0, "play failed");
    }
    rig.topUp();
    rig.discard(1024);
    EXPECT(remixFrom(rig, rig.engine.getPlayheadTime()),
           "re-mix should work before the resize");

    EXPECT(rig.engine.setMaxActiveVoiceCount(2) == SoLoud::SO_NO_ERROR,
           "setMaxActiveVoiceCount failed");
    EXPECT(!remixFrom(rig, rig.engine.getPlayheadTime()),
           "a checkpoint from before the resize was restored");

    rig.engine.stopAll();
}

// Echo snapshots are refilled in place at each checkpoint instead of being
// reallocated; a refilled snapshot must restore exactly like a fresh one, and
// a snapshot of another type or size must be refused.
static void testEchoSnapshotRecycling() {
    std::printf("Test: echo snapshots are refilled in place\n");

    SoLoud::EchoFilter echo;
    echo.setParams(0.05f, 0.5f, 0.0f);
    SoLoud::FilterInstance *inst = echo.createInstance();
    std::vector<float> buf(2 * 512);
    auto feed = [&](float level) {
        std::fill(buf.begin(), buf.end(), level);
        inst->filter(buf.data(), 512, 512, 2, (float)kSampleRate, 0.0);
    };

    feed(0.3f);
    SoLoud::FilterStateSnapshot *snap = inst->captureState();
    EXPECT(snap != nullptr && snap->mKind != nullptr, "echo snapshot has no kind");
    feed(0.7f);
    EXPECT(inst->captureStateInto(snap), "echo refused its own snapshot");

    // Diverge, restore the refilled snapshot, and compare with a reference
    // instance that never diverged.
    std::vector<float> expected(2 * 512);
    {
        SoLoud::FilterInstance *ref = echo.createInstance();
        std::fill(buf.begin(), buf.end(), 0.3f);
        ref->filter(buf.data(), 512, 512, 2, (float)kSampleRate, 0.0);
        std::fill(buf.begin(), buf.end(), 0.7f);
        ref->filter(buf.data(), 512, 512, 2, (float)kSampleRate, 0.0);
        std::fill(expected.begin(), expected.end(), 0.1f);
        ref->filter(expected.data(), 512, 512, 2, (float)kSampleRate, 0.0);
        delete ref;
    }
    feed(-0.9f);
    inst->restoreState(snap);
    std::fill(buf.begin(), buf.end(), 0.1f);
    inst->filter(buf.data(), 512, 512, 2, (float)kSampleRate, 0.0);
    bool same = true;
    for (size_t i = 0; i < buf.size(); ++i) {
        if (std::fabs(buf[i] - expected[i]) > 1e-6f) {
            same = false;
            break;
        }
    }
    EXPECT(same, "restoring a refilled echo snapshot did not reproduce the output");

    SoLoud::BiquadResonantFilter biquad;
    SoLoud::FilterInstance *bq = biquad.createInstance();
    SoLoud::FilterStateSnapshot *foreign = bq->captureState();
    EXPECT(!inst->captureStateInto(foreign), "echo accepted a biquad snapshot");
    delete foreign;
    delete bq;

    SoLoud::EchoFilter longEcho;
    longEcho.setParams(0.5f, 0.5f, 0.0f);
    SoLoud::FilterInstance *other = longEcho.createInstance();
    std::fill(buf.begin(), buf.end(), 0.2f);
    other->filter(buf.data(), 512, 512, 2, (float)kSampleRate, 0.0);
    EXPECT(!other->captureStateInto(snap), "echo accepted a snapshot of another size");
    delete other;

    delete snap;
    delete inst;
}

} // namespace rt_engine

// 6. reset() clears the underrun statistics.
static void testRealtimeResetClearsUnderrunCount() {
    std::printf("Test: RealtimeStream reset clears unread frames and underrun count\n");

    SoLoud::RealtimeStream stream;
    EXPECT(stream.init(nullptr, 48000, 1, BufferType::PCM_F32LE, 1024) ==
               PlayerErrors::noError,
           "init failed");
    SoLoud::AudioSourceInstance *inst = stream.createInstance();

    std::vector<float> in(100, 0.5f);
    std::vector<float> out(128, 0.0f);
    stream.pushDataFloat32(in.data(), 100);
    inst->getAudio(out.data(), 128, 128);  // 100 frames, then an underrun
    EXPECT(stream.getUnderrunCount() == 1, "underrunCount != 1 (%u)",
           stream.getUnderrunCount());

    stream.pushDataFloat32(in.data(), 100);
    stream.resetBuffer();
    EXPECT(stream.getUnderrunCount() == 0, "reset did not clear underrunCount (%u)",
           stream.getUnderrunCount());
    EXPECT(stream.getAvailableToRead() == 0, "reset left %u unread frames",
           stream.getAvailableToRead());

    delete inst;
}

int main() {
    testRealtimeRingBuffer();
    testRealtimeStream();
    testCustomAudioSource();
    testCustomFilter();
    rt_engine::testScheduledAfterQueuedAudio();
    rt_engine::testScheduledInWindowKeepsEarlierAudio();
    rt_engine::testImmediatePushResumesAtPlayhead();
    rt_engine::testResetSurvivesRemix();
    rt_engine::testResetDuringScheduledGap();
    rt_engine::testRenderAheadGlobalFilterThenClip();
    rt_engine::testRemixRefusedWithStatefulCustomDsp();
    rt_engine::testMaxActiveVoiceShrinkRetiresCheckpoints();
    rt_engine::testEchoSnapshotRecycling();
    testRealtimeResetClearsUnderrunCount();

    std::printf("\n%d assertions, %d failures\n", g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}

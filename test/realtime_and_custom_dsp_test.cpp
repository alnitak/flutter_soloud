// Standalone C++ unit tests for RealtimeRingBuffer, RealtimeStream,
// CustomAudioSource, CustomFilter, and uint64_t FilterInstance::mParamChanged.
//
// Build & run from the flutter_soloud repo root:
//
//   ./test/run_realtime_and_custom_dsp_test.sh

#include "../src/audiobuffer/realtime_ring_buffer.h"
#include "../src/audiobuffer/realtime_stream.h"
#include "../src/filters/custom_filter.h"
#include "../src/synth/custom_source.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
}

// 2. Test RealtimeStream & RealtimeStreamInstance (underrun, capture/restore state, cosine anti-click ramp)
static void testRealtimeStream() {
    std::printf("Test: RealtimeStream (zero-wait pull, state capture/restore, cosine anti-click)\n");

    SoLoud::RealtimeStream stream;
    PlayerErrors err = stream.init(nullptr, nullptr, 48000, 2,
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

    delete inst;

    // Test built-in named DSP callbacks ("soloudOverdriveFilterCallback", "soloudSvfFilterCallback")
    SoLoud::CustomFilter overdriveFilter;
    overdriveFilter.setParamDefinition(0, 1.0f, 0.0f, 1.0f, "Wet");
    overdriveFilter.setParamDefinition(1, 3.0f, 1.0f, 10.0f, "Drive");
    overdriveFilter.setParamDefinition(2, 0.2f, -0.5f, 0.5f, "Bias");
    overdriveFilter.setParamDefinition(3, 0.8f, 0.0f, 1.0f, "Output Gain");
    overdriveFilter.setWebJsCallbackName("soloudOverdriveFilterCallback");
    SoLoud::FilterInstance *odInst = overdriveFilter.createInstance();
    std::fill(buf.begin(), buf.end(), 0.5f);
    odInst->filter(buf.data(), 64, 64, 2, 48000.0f, 0.0);
    EXPECT(std::fabs(buf[0] - 0.5f) > 1e-3f && std::fabs(buf[0]) < 1.0f,
           "soloudOverdriveFilterCallback did not process buffer (%.4f)", buf[0]);
    delete odInst;

    SoLoud::CustomFilter svfFilter;
    svfFilter.setParamDefinition(0, 1.0f, 0.0f, 1.0f, "Wet");
    svfFilter.setParamDefinition(1, 1200.0f, 80.0f, 12000.0f, "Cutoff Hz");
    svfFilter.setParamDefinition(2, 0.45f, 0.0f, 0.95f, "Resonance");
    svfFilter.setWebJsCallbackName("soloudSvfFilterCallback");
    SoLoud::FilterInstance *svfInst = svfFilter.createInstance();
    std::fill(buf.begin(), buf.end(), 1.0f);
    svfInst->filter(buf.data(), 64, 64, 2, 48000.0f, 0.0);
    EXPECT(buf[0] > 0.0f && buf[63] > buf[0],
           "soloudSvfFilterCallback step response invalid (buf[0]=%.4f, buf[63]=%.4f)",
           buf[0], buf[63]);
    delete svfInst;

    SoLoud::CustomAudioSource namedSynth(48000, 2, nullptr, nullptr, nullptr,
                                         false, "soloudCustomSynthCallback");
    SoLoud::AudioSourceInstance *namedSynthInst = namedSynth.createInstance();
    std::fill(buf.begin(), buf.end(), 0.0f);
    unsigned int synthRead = namedSynthInst->getAudio(buf.data(), 64, 64);
    EXPECT(synthRead == 64 && (std::fabs(buf[10]) > 1e-5f || std::fabs(buf[74]) > 1e-5f),
           "soloudCustomSynthCallback did not synthesize audio");
    delete namedSynthInst;
}

int main() {
    testRealtimeRingBuffer();
    testRealtimeStream();
    testCustomAudioSource();
    testCustomFilter();

    std::printf("\n%d assertions, %d failures\n", g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}

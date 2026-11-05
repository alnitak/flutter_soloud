// Standalone tests for the resampler's block carry (flutter_soloud local
// patch "resampler carry" in Soloud::mixBus_internal).
//
// The mixer reads a voice in blocks of SAMPLE_GRANULARITY (512) source
// samples and resamples one block at a time. Output sample k must read the
// source at the sum of the first k steps, wherever the block edges fall, so
// the output matches one resampler pass over the whole source bit for bit.
// Before the patch the mixer wrote one output sample too few per block and
// then cropped the position to the start of the next block: whenever the
// step did not divide 512 it skipped up to one step per block (a click on
// pitched sounds, and voices running fast, jarikomppa/soloud#314), and a
// step over 512 samples hung the mixer (jarikomppa/soloud#343).
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_resampler_carry_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"
#include "soloud_bus.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

constexpr int kFracBits = 20; // FIXPOINT_FRAC_BITS in soloud.cpp
constexpr long long kFrac = 1LL << kFracBits;

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

// ---- Deterministic test source --------------------------------------------

// A sawtooth with a period of 1000 samples, so block edges (every 512) fall
// on every phase. Sample i is a pure function of i; samples before 0 are 0,
// like the zeroed resample block in front of a new voice.
float sawSample(long long i)
{
    if (i < 0)
        return 0.0f;
    return (float)((double)(i % 1000) / 1000.0 - 0.5);
}

class SawInstance final : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                          unsigned int /*aBufferSize*/) override
    {
        for (unsigned int i = 0; i < aSamplesToRead; i++)
            aBuffer[i] = sawSample(mNext + i);
        mNext += aSamplesToRead;
        return aSamplesToRead;
    }
    bool hasEnded() override { return false; }
    long long mNext = 0;
};

class Saw final : public SoLoud::AudioSource {
public:
    explicit Saw(float aSamplerate)
    {
        mBaseSamplerate = aSamplerate;
        mChannels = 1;
    }
    SoLoud::AudioSourceInstance *createInstance() override
    {
        return new SawInstance();
    }
};

// ---- One resampler pass over the whole source ------------------------------

// catmullrom() from soloud.cpp, verbatim.
float catmullrom(float t, float p0, float p1, float p2, float p3)
{
    return 0.5f * (
        (2 * p1) +
        (-p0 + p2) * t +
        (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t +
        (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t
        );
}

// What resample_point/linear/catmullrom compute at the absolute fixed-point
// source position [aPos].
float expectedAt(unsigned int aResampler, long long aPos)
{
    const long long p = aPos >> kFracBits;
    const int f = (int)(aPos & (kFrac - 1));
    switch (aResampler)
    {
    case SoLoud::Soloud::RESAMPLER_POINT:
        return sawSample(p);
    case SoLoud::Soloud::RESAMPLER_CATMULLROM:
        return catmullrom(f / (float)kFrac, sawSample(p - 3), sawSample(p - 2),
                          sawSample(p - 1), sawSample(p));
    default:
    {
        const float s1 = sawSample(p - 1), s2 = sawSample(p);
        return s1 + (s2 - s1) * f * (1 / (float)kFrac);
    }
    }
}

// ---- Rendering --------------------------------------------------------------

struct Case {
    Case(const char *aName, float aSourceRate, unsigned int aEngineRate,
         std::function<float(int)> aSpeedAt,
         std::function<unsigned int(int)> aChunkAt = nullptr)
        : name(aName), sourceRate(aSourceRate), engineRate(aEngineRate),
          speedAt(std::move(aSpeedAt)), chunkAt(std::move(aChunkAt)) {}

    const char *name;
    float sourceRate;
    unsigned int engineRate;
    std::function<float(int)> speedAt;          // per mix() call
    std::function<unsigned int(int)> chunkAt;   // frames per mix() call
    unsigned int resampler = SoLoud::Soloud::RESAMPLER_LINEAR;
    bool viaBus = false;
    unsigned int frames = 0;
    // Called before each mix() call, e.g. to flag the voice inaudible.
    std::function<void(SoLoud::AudioSourceInstance *, int)> beforeChunk;
};

struct Result {
    std::vector<float> out;
    std::vector<long long> pos; // where each output sample must read
    long long next = 0;         // where the next one must read
    long long mixerNext = -1;   // where the mixer will read next
};

Result render(const Case &c)
{
    SoLoud::Soloud soloud;
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, c.engineRate, 512, 1);
    soloud.setPostClipScaler(1.0f);
    soloud.setGlobalVolume(1.0f);
    soloud.setMainResampler(c.resampler);
    Saw saw(c.sourceRate);
    SoLoud::Bus bus;
    SoLoud::handle voice;
    if (c.viaBus)
    {
        bus.setChannels(1);
        const SoLoud::handle b = soloud.play(bus, 1.0f, 0.0f, true);
        soloud.setPanAbsolute(b, 1.0f, 1.0f);
        soloud.setVolume(b, 1.0f);
        soloud.setPause(b, false);
        voice = bus.play(saw, 1.0f, 0.0f, true);
    }
    else
    {
        voice = soloud.play(saw, 1.0f, 0.0f, true);
    }
    // Unity gain, set while paused so the first buffer is not a ramp.
    soloud.setPanAbsolute(voice, 1.0f, 1.0f);
    soloud.setVolume(voice, 1.0f);
    soloud.setPause(voice, false);

    const float mixRate = c.viaBus ? bus.mBaseSamplerate : (float)c.engineRate;
    Result r;
    std::vector<float> buffer;
    for (int chunk = 0; r.out.size() < c.frames; chunk++)
    {
        soloud.setRelativePlaySpeed(voice, c.speedAt(chunk));
        SoLoud::AudioSourceInstance *v =
            soloud.mVoice[soloud.getVoiceFromHandle_internal(voice)];
        if (c.beforeChunk)
            c.beforeChunk(v, chunk);
        // The step as mixBus_internal computes it.
        const float step = v->mSamplerate / mixRate;
        const long long stepFixed = (unsigned int)(int)std::floor(step * (int)kFrac);
        const unsigned int n = std::min<unsigned int>(
            c.chunkAt(chunk), c.frames - (unsigned int)r.out.size());
        buffer.assign(n, 0.0f);
        soloud.mix(buffer.data(), n);
        for (unsigned int i = 0; i < n; i++)
        {
            r.out.push_back(buffer[i]);
            r.pos.push_back(r.next);
            r.next += stepFixed;
        }
    }
    if (!c.viaBus)
    {
        // The current block holds source samples [mNext - 512, mNext), and
        // mSrcOffset is the next read relative to its start.
        SoLoud::AudioSourceInstance *v =
            soloud.mVoice[soloud.getVoiceFromHandle_internal(voice)];
        r.mixerNext = (static_cast<SawInstance *>(v)->mNext - 512) * kFrac +
                      (long long)v->mSrcOffset;
    }
    soloud.deinit();
    return r;
}

// Output samples that differ from one pass over the whole source, and the
// first of them (-1 if none).
std::pair<long, long> countDiffering(const Case &c, const Result &r,
                                     std::function<bool(long)> silent = nullptr)
{
    long count = 0, first = -1;
    for (size_t k = 0; k < r.out.size(); k++)
    {
        float expected;
        if (silent && silent((long)k))
            expected = 0.0f;
        else if (c.viaBus)
            // The root reads the bus at step 1, fraction 0: the bus arrives
            // one sample late.
            expected = k == 0 ? 0.0f : expectedAt(c.resampler, r.pos[k - 1]);
        else
            expected = expectedAt(c.resampler, r.pos[k]);
        if (r.out[k] != expected)
        {
            if (first < 0)
                first = (long)k;
            count++;
        }
    }
    return {count, first};
}

const char *resamplerName(unsigned int aResampler)
{
    switch (aResampler)
    {
    case SoLoud::Soloud::RESAMPLER_POINT: return "point";
    case SoLoud::Soloud::RESAMPLER_CATMULLROM: return "catmullrom";
    default: return "linear";
    }
}

// Mix call sizes from 1 to 4096 frames, as a device callback may ask.
unsigned int oddChunk(int i)
{
    unsigned int x = (unsigned int)i * 2654435761u + 12345u;
    x ^= x >> 13;
    x *= 0x5bd1e995u;
    x ^= x >> 15;
    return 1 + x % 4096;
}

std::function<float(int)> constant(float aSpeed)
{
    return [aSpeed](int) { return aSpeed; };
}

// ---- Tests ------------------------------------------------------------------

// Every resampler at speeds and rate ratios that do and do not divide 512,
// with even and uneven mix calls: bit for bit against one pass, and the
// mixer's next read where the steps put it.
void testPositions()
{
    std::fprintf(stderr, "testPositions\n");
    const std::vector<Case> cases = {
        {"1.0", 44100, 44100, constant(1.0f)},
        {"0.5", 44100, 44100, constant(0.5f)},
        {"2.0", 44100, 44100, constant(2.0f)},
        {"0.8", 44100, 44100, constant(0.8f)},
        {"1.3", 44100, 44100, constant(1.3f)},
        {"0.01", 44100, 44100, constant(0.01f)},
        {"7.9", 44100, 44100, constant(7.9f)},
        {"22.05k file at 1.1", 22050, 44100, constant(1.1f)},
        {"22.05k file, 48k engine", 22050, 48000, constant(1.0f)},
        {"44.1k file, 48k engine", 44100, 48000, constant(1.0f)},
        {"glide 0.8 to 1.25", 44100, 44100,
         [](int chunk) { return 0.8f + 0.45f * (float)(0.5 - 0.5 * std::cos(chunk * 0.05)); }},
    };
    for (unsigned int resampler : {SoLoud::Soloud::RESAMPLER_POINT,
                                   SoLoud::Soloud::RESAMPLER_LINEAR,
                                   SoLoud::Soloud::RESAMPLER_CATMULLROM})
    {
        for (const Case &base : cases)
        {
            for (int uneven = 0; uneven < 2; uneven++)
            {
                Case c = base;
                c.resampler = resampler;
                c.chunkAt = uneven ? oddChunk : [](int) { return 512u; };
                c.frames = 2 * c.engineRate;
                const Result r = render(c);
                const auto [count, first] = countDiffering(c, r);
                EXPECT(count == 0,
                       "%s, %s, %s mix calls: %ld of %zu samples differ, first at %ld",
                       resamplerName(resampler), c.name, uneven ? "uneven" : "512-frame",
                       count, r.out.size(), first);
                EXPECT(r.mixerNext == r.next,
                       "%s, %s, %s mix calls: the mixer reads %.3f source samples "
                       "ahead after %u frames",
                       resamplerName(resampler), c.name, uneven ? "uneven" : "512-frame",
                       (double)(r.mixerNext - r.next) / (double)kFrac, c.frames);
            }
        }
    }
}

// A voice at 1.3x inside a bus: the bus mixes it like the engine would.
void testBus()
{
    std::fprintf(stderr, "testBus\n");
    Case c{"bus", 44100, 44100, constant(1.3f), oddChunk};
    c.viaBus = true;
    c.frames = 2 * c.engineRate;
    const Result r = render(c);
    const auto [count, first] = countDiffering(c, r);
    EXPECT(count == 0, "voice at 1.3 in a bus: %ld of %zu samples differ, first at %ld",
           count, r.out.size(), first);
}

// An inaudible voice that must tick (the second branch of mixBus_internal)
// advances exactly like an audible one, so it comes back in step.
void testInaudibleTick()
{
    std::fprintf(stderr, "testInaudibleTick\n");
    Case c{"inaudible", 44100, 44100, constant(1.3f), [](int) { return 512u; }};
    c.frames = 3 * c.engineRate;
    const int from = 40, to = 120; // mix calls without sound
    c.beforeChunk = [&](SoLoud::AudioSourceInstance *v, int chunk) {
        if (chunk == from)
            v->mFlags |= SoLoud::AudioSourceInstance::INAUDIBLE |
                         SoLoud::AudioSourceInstance::INAUDIBLE_TICK;
        if (chunk == to)
            v->mFlags &= ~SoLoud::AudioSourceInstance::INAUDIBLE;
    };
    const Result r = render(c);
    const auto [count, first] = countDiffering(c, r, [&](long k) {
        return k >= from * 512L && k < to * 512L;
    });
    EXPECT(count == 0,
           "voice at 1.3 inaudible for %d mix calls: %ld of %zu samples differ, first at %ld",
           to - from, count, r.out.size(), first);
}

void onHang(int)
{
    static const char message[] =
        "  FAIL: mixing a step of 600 source samples did not finish in 10 s\n";
    (void)!write(2, message, sizeof message - 1);
    _exit(1);
}

// A step over 512 source samples per output sample used to leave no output
// sample in any block and loop forever (jarikomppa/soloud#343).
void testLargeStep()
{
    std::fprintf(stderr, "testLargeStep\n");
    std::signal(SIGALRM, onHang);
    alarm(10);
    Case c{"600", 44100, 44100, constant(600.0f), [](int) { return 512u; }};
    c.resampler = SoLoud::Soloud::RESAMPLER_POINT;
    c.frames = 2048;
    const Result r = render(c);
    alarm(0);
    const auto [count, first] = countDiffering(c, r);
    EXPECT(count == 0, "speed 600: %ld of %zu samples differ, first at %ld", count,
           r.out.size(), first);
}

} // namespace

int main()
{
    testPositions();
    testBus();
    testInaudibleTick();
    testLargeStep();

    std::fprintf(stderr, "\n%d assertions, %d failures\n", gAssertions,
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}

// Standalone tests for the end of a voice that does not loop (flutter_soloud
// local patch "voice tail" in Soloud::mixBus_internal).
//
// The mixer reads a voice in blocks of SAMPLE_GRANULARITY (512) source
// samples, up to one block ahead of the output. A source reports hasEnded()
// as soon as its last block has been read, and the mixer used to stop the
// voice at the end of that pass, throwing away whatever of the block was not
// output yet: up to 512 source samples (11.6 ms at 44.1 kHz). Handing a
// sound off to a copy scheduled at its end left a gap at the seam. Now the
// voice is stopped once a read made after the end comes back empty, or as
// soon as it is no longer mixed.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_voice_tail_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

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

constexpr unsigned int kBlock = 512; // SAMPLE_GRANULARITY
constexpr int kFracBits = 20;        // FIXPOINT_FRAC_BITS in soloud.cpp
constexpr long long kFrac = 1LL << kFracBits;
constexpr double kStart = 0.01;      // engine time the first voice starts at

// ---- Test sound -------------------------------------------------------------

// Deterministic noise in [0.2, 0.5): never near zero, so every resampler
// stays clear of zero inside the sound, and a silent output frame is a gap.
float noiseSample(unsigned int i)
{
    unsigned int x = i * 2654435761u + 0x9e3779b9u;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return 0.2f + 0.3f * (float)(x >> 8) / 16777216.0f;
}

bool silent(float aSample)
{
    return std::fabs(aSample) < 1e-6f;
}

// A sound in memory that reads and ends like WavInstance: getAudio() returns
// what is left, and hasEnded() turns true as soon as the last sample has
// been read. Kept to the engine core so the test needs no decoders.
class Noise;

class NoiseInstance final : public SoLoud::AudioSourceInstance {
public:
    explicit NoiseInstance(const Noise &aParent) : mParent(aParent) {}
    unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                          unsigned int aBufferSize) override;
    bool hasEnded() override;
    SoLoud::result rewind() override
    {
        mOffset = 0;
        mStreamPosition = 0;
        return SoLoud::SO_NO_ERROR;
    }

private:
    const Noise &mParent;
    unsigned int mOffset = 0;
};

class Noise final : public SoLoud::AudioSource {
public:
    Noise(unsigned int aLength, float aSamplerate) : mData(aLength)
    {
        for (unsigned int i = 0; i < aLength; i++)
            mData[i] = noiseSample(i);
        mBaseSamplerate = aSamplerate;
        mChannels = 1;
    }
    ~Noise() override { stop(); }
    SoLoud::AudioSourceInstance *createInstance() override
    {
        return new NoiseInstance(*this);
    }
    // As Wav::getLength().
    double getLength() const { return mData.size() / mBaseSamplerate; }

    std::vector<float> mData;
};

unsigned int NoiseInstance::getAudio(float *aBuffer, unsigned int aSamplesToRead,
                                     unsigned int /*aBufferSize*/)
{
    const unsigned int n = std::min<unsigned int>(
        aSamplesToRead, (unsigned int)mParent.mData.size() - mOffset);
    std::copy(mParent.mData.begin() + mOffset,
              mParent.mData.begin() + mOffset + n, aBuffer);
    mOffset += n;
    return n;
}

bool NoiseInstance::hasEnded()
{
    return !(mFlags & LOOPING) && mOffset >= mParent.mData.size();
}

// Sound lengths whose ends fall on every phase of the 512-sample blocks.
std::vector<unsigned int> lengths(unsigned int aCount)
{
    std::vector<unsigned int> r;
    for (unsigned int j = 0; j < aCount; j++)
        r.push_back(3000 + j * 997);
    return r;
}

// ---- Engine setup -----------------------------------------------------------

struct Rates {
    const char *name;
    float source;
    unsigned int engine;
};

const Rates kRates[] = {
    {"44.1k in 48k", 44100.0f, 48000},
    {"48k in 44.1k", 48000.0f, 44100},
    {"22.05k in 48k", 22050.0f, 48000},
};

const unsigned int kResamplers[] = {
    SoLoud::Soloud::RESAMPLER_POINT,
    SoLoud::Soloud::RESAMPLER_LINEAR,
    SoLoud::Soloud::RESAMPLER_CATMULLROM,
};

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

unsigned int chunkSize(bool aUneven, int aCall)
{
    return aUneven ? oddChunk(aCall) : kBlock;
}

void initEngine(SoLoud::Soloud &aSoloud, unsigned int aEngineRate,
                unsigned int aResampler)
{
    aSoloud.init(0, SoLoud::Soloud::NULLDRIVER, aEngineRate, kBlock, 1);
    aSoloud.setPostClipScaler(1.0f);
    aSoloud.setGlobalVolume(1.0f);
    aSoloud.setMainResampler(aResampler);
}

unsigned int frameAt(double aTime, unsigned int aEngineRate)
{
    return (unsigned int)std::floor(aTime * aEngineRate + 0.5);
}

// The step as mixBus_internal computes it, in 12.20 fixed point.
long long stepFixedOf(float aSourceRate, unsigned int aEngineRate)
{
    const float step = aSourceRate / (float)aEngineRate;
    return (long long)(unsigned int)(int)std::floor(step * (int)kFrac);
}

// The mixer reads source block b when it needs the first output sample whose
// position, k * step, lies at or after b * 512. This is the output sample,
// counted from the voice's start, at which it reads past the last block of
// a sound of aLength samples: that read comes back empty.
long readPastEnd(unsigned int aLength, long long aStepFixed)
{
    const long long end = (long long)((aLength + kBlock - 1) / kBlock) * kBlock;
    return (long)((end * kFrac + aStepFixed - 1) / aStepFixed);
}

// ---- Tests ------------------------------------------------------------------

// A sound that does not loop, handed off to a looping copy scheduled at its
// end with playScheduled(start + getLength()): the output must not fall
// silent anywhere from the first sound on. Before the patch the first voice
// lost the unplayed rest of its last block, so most seams gapped by up to
// 512 source samples.
void testSeam()
{
    std::fprintf(stderr, "testSeam\n");
    const std::vector<unsigned int> lens = lengths(75);
    for (const Rates &rates : kRates)
    {
        for (unsigned int resampler : kResamplers)
        {
            for (int uneven = 0; uneven < 2; uneven++)
            {
                unsigned int gapped = 0, longest = 0, longestLength = 0;
                for (unsigned int length : lens)
                {
                    SoLoud::Soloud soloud;
                    initEngine(soloud, rates.engine, resampler);
                    Noise noise(length, rates.source);

                    soloud.playScheduled(kStart, noise, 1.0f);
                    const double seam = kStart + noise.getLength();
                    soloud.playScheduled(seam, noise, 1.0f, 0.0f, 0, 1.0f, true);

                    const unsigned int frames = frameAt(seam, rates.engine) + 4096;
                    std::vector<float> out;
                    std::vector<float> buffer;
                    for (int call = 0; out.size() < frames; call++)
                    {
                        const unsigned int n = chunkSize(uneven, call);
                        buffer.assign(n, 0.0f);
                        soloud.mix(buffer.data(), n);
                        out.insert(out.end(), buffer.begin(), buffer.end());
                    }
                    out.resize(frames);
                    soloud.deinit();

                    // Longest run of silence after the first sound.
                    size_t k = 0;
                    while (k < out.size() && silent(out[k]))
                        k++;
                    unsigned int run = 0, worst = 0;
                    for (; k < out.size(); k++)
                    {
                        run = silent(out[k]) ? run + 1 : 0;
                        if (run > worst)
                            worst = run;
                    }
                    if (worst > 0)
                        gapped++;
                    if (worst > longest)
                    {
                        longest = worst;
                        longestLength = length;
                    }
                }
                EXPECT(gapped == 0,
                       "%s, %s, %s mix calls: %u of %zu seams gap, the longest by "
                       "%u frames (a %u-sample sound)",
                       rates.name, resamplerName(resampler),
                       uneven ? "uneven" : "512-frame", gapped, lens.size(),
                       longest, longestLength);
            }
        }
    }
}

// A single voice that does not loop plays to its last sample and is stopped
// in the mix call that reads past its last block, no earlier and no later,
// and a twin that is inaudible but ticking (the second branch of
// mixBus_internal) is stopped in the same mix call.
void testTail()
{
    std::fprintf(stderr, "testTail\n");
    const std::vector<unsigned int> lens = lengths(25);
    for (const Rates &rates : kRates)
    {
        const double step = (double)rates.source / rates.engine;
        const long long stepFixed = stepFixedOf(rates.source, rates.engine);
        for (unsigned int resampler : kResamplers)
        {
            for (int uneven = 0; uneven < 2; uneven++)
            {
                for (unsigned int length : lens)
                {
                    SoLoud::Soloud soloud;
                    initEngine(soloud, rates.engine, resampler);
                    Noise noise(length, rates.source);

                    const SoLoud::handle voice = soloud.playScheduled(kStart, noise, 1.0f);
                    const SoLoud::handle twin = soloud.playScheduled(kStart, noise, 1.0f);
                    soloud.mVoice[soloud.getVoiceFromHandle_internal(twin)]->mFlags |=
                        SoLoud::AudioSourceInstance::INAUDIBLE |
                        SoLoud::AudioSourceInstance::INAUDIBLE_TICK;

                    const long startFrame = frameAt(kStart, rates.engine);
                    const long emptyRead = startFrame + readPastEnd(length, stepFixed);
                    const long limit = startFrame + (long)(length / step) + 4 * 4096;
                    long total = 0, lastSound = -1;
                    int voiceStop = -1, twinStop = -1, expectedStop = -1;
                    std::vector<float> buffer;
                    for (int call = 0;
                         (voiceStop < 0 || twinStop < 0 || expectedStop < 0) && total < limit;
                         call++)
                    {
                        const unsigned int n = chunkSize(uneven, call);
                        buffer.assign(n, 0.0f);
                        soloud.mix(buffer.data(), n);
                        for (unsigned int i = 0; i < n; i++)
                            if (!silent(buffer[i]))
                                lastSound = total + i;
                        if (expectedStop < 0 && total + n > emptyRead)
                            expectedStop = call;
                        if (voiceStop < 0 && !soloud.isValidVoiceHandle(voice))
                            voiceStop = call;
                        if (twinStop < 0 && !soloud.isValidVoiceHandle(twin))
                            twinStop = call;
                        total += n;
                    }
                    soloud.deinit();

                    // The source's last sample is read at output frame
                    // ceil(length / step) - 1 or later, by every resampler.
                    const long lastExpected = startFrame + (long)(length / step) - 1;
                    EXPECT(lastSound >= lastExpected,
                           "%s, %s, %s mix calls, %u samples: the sound stops %ld "
                           "frames early",
                           rates.name, resamplerName(resampler),
                           uneven ? "uneven" : "512-frame", length,
                           lastExpected - lastSound);
                    EXPECT(voiceStop == expectedStop,
                           "%s, %s, %s mix calls, %u samples: the voice is stopped "
                           "in mix call %d, expected %d (the one holding output "
                           "frame %ld, which reads past the last block)",
                           rates.name, resamplerName(resampler),
                           uneven ? "uneven" : "512-frame", length, voiceStop,
                           expectedStop, emptyRead);
                    EXPECT(twinStop == voiceStop,
                           "%s, %s, %s mix calls, %u samples: the ticking twin is "
                           "stopped in mix call %d, the audible voice in %d",
                           rates.name, resamplerName(resampler),
                           uneven ? "uneven" : "512-frame", length, twinStop,
                           voiceStop);
                }
            }
        }
    }
}

// A voice that read its last block but is not mixed any more must not keep
// its unplayed tail: it is stopped by the next mix call. Two ways out of the
// mix: 3D attenuation makes it inaudible (update3dAudio flags it INAUDIBLE,
// without ticking), or louder voices push it beyond the active voice count.
// Without the stop the voice stayed alive as long as it was not mixed, and
// played its stale tail if it was mixed again.
void testUnmixed()
{
    std::fprintf(stderr, "testUnmixed\n");
    const std::vector<unsigned int> lens = lengths(25);
    const char *const ways[] = {"inaudible through 3D attenuation",
                                "beyond the active voice count"};
    for (int way = 0; way < 2; way++)
    {
        unsigned int pending = 0, alive = 0, aliveLength = 0;
        for (unsigned int length : lens)
        {
            SoLoud::Soloud soloud;
            soloud.init(0, SoLoud::Soloud::NULLDRIVER, 48000, kBlock, 2);
            Noise noise(length, 44100.0f);
            Noise loud(4096, 44100.0f);
            SoLoud::handle voice;
            if (way == 0)
            {
                voice = soloud.play3d(noise, 0.0f, 0.0f, 0.0f);
                soloud.set3dSourceAttenuation(voice, SoLoud::AudioSource::INVERSE_DISTANCE, 1.0f);
                soloud.set3dSourceMinMaxDistance(voice, 1.0f, 1e6f);
                soloud.update3dAudio();
            }
            else
            {
                soloud.setMaxActiveVoiceCount(1);
                voice = soloud.play(noise, 0.5f);
            }

            // Mix until the voice has read its last block, while it is still
            // alive with the rest of that block to play.
            std::vector<float> buffer(2 * kBlock);
            bool tailPending = false;
            for (int call = 0; call < 1000 && soloud.isValidVoiceHandle(voice); call++)
            {
                soloud.mix(buffer.data(), kBlock);
                if (soloud.isValidVoiceHandle(voice) &&
                    soloud.mVoice[soloud.getVoiceFromHandle_internal(voice)]->hasEnded())
                {
                    tailPending = true;
                    break;
                }
            }
            if (!tailPending)
            {
                soloud.deinit();
                continue; // the read past the end fell in the same mix call
            }
            pending++;

            if (way == 0)
            {
                soloud.set3dSourcePosition(voice, 1e4f, 0.0f, 0.0f);
                soloud.update3dAudio();
            }
            else
            {
                const SoLoud::handle louder = soloud.play(loud, 1.0f);
                soloud.setLooping(louder, true);
            }
            soloud.mix(buffer.data(), kBlock);
            if (soloud.isValidVoiceHandle(voice))
            {
                alive++;
                aliveLength = length;
            }
            soloud.deinit();
        }
        EXPECT(pending > 0,
               "%s: no voice was ever left alive with the rest of its last block "
               "to play, so the case is not exercised", ways[way]);
        EXPECT(alive == 0,
               "%s: %u of %u voices with the rest of their last block to play stay "
               "alive (a %u-sample sound, for one)",
               ways[way], alive, pending, aliveLength);
    }
}

} // namespace

int main()
{
    testSeam();
    testTail();
    testUnmixed();

    std::fprintf(stderr, "\n%d assertions, %d failures\n", gAssertions,
                 gFailures);
    return gFailures == 0 ? 0 : 1;
}

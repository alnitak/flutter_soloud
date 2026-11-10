// Standalone native tests for the waveform synthesizer (Basicwave, the
// waveform generator in soloud_misc.cpp) and the plugin calls that create and
// change it.
//
// Scenarios (pass one name to run only that one):
//
//   fourier  The band-limited square and saw (WAVE_FSQUARE, WAVE_FSAW) look
//            up a table of 4097 entries. Every phase must stay inside it: a
//            tiny negative phase wraps to exactly 1.0f, and a NaN or
//            infinite phase has no table position at all (its float to int
//            conversion is undefined). Built with --asan, a read past the
//            table is reported. A waveform at a tiny negative frequency, and
//            one whose frequency is NaN, are rendered too.
//
//   freq     setWaveformFreq() must refuse a NaN or infinite frequency: it
//            would make the oscillator's phase non-finite for good.
//
//   params   loadWaveform(waveform, superWave, scale, detune) must build the
//            waveform with that scale and that detune, not the two swapped.
//
//   race     The setWaveform*() calls change a waveform while the audio
//            thread renders it. The parameters the mixer reads must not be
//            written unsynchronized; run under ThreadSanitizer (--tsan),
//            which reports the setters racing BasicwaveInstance::getAudio().
//
// `freq`, `params` and `race` open the default output device through the
// plugin's initEngine() (miniaudio falls back to its null backend without
// one; `race` plays at a volume of 0.001); `fourier` renders waveform
// instances directly and needs no engine.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_waveform_synth_test.sh [--asan|--tsan] [scenario]

#include "enums.h"
#include "player.h"
#include "synth/basic_wave.h"

#include "soloud.h"
#include "soloud_misc.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

extern "C"
{
    extern std::unique_ptr<Player> &player;
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency,
                                 unsigned int devicePeriodFrames,
                                 unsigned int renderAheadFrames);
    void dispose();
    enum PlayerErrors loadWaveform(int waveform, bool superWave, float scale,
                                   float detune, unsigned int *hash);
    void setWaveformScale(unsigned int hash, float newScale);
    void setWaveformDetune(unsigned int hash, float newDetune);
    void setWaveformFreq(unsigned int hash, float newFreq);
    void setSuperWave(unsigned int hash, bool superwave);
    void setWaveform(unsigned int hash, int newWaveform);
    enum PlayerErrors play(unsigned int soundHash, unsigned int busId, float volume,
                           float pan, bool paused, bool looping,
                           double loopingStartAt, double loopingEndAt,
                           int loopingStartOffsetAt, int loopingEndOffsetAt,
                           float scale, unsigned int *handle);
    double getPosition(unsigned int handle);
    int getIsValidVoiceHandle(unsigned int handle);
}

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

bool initPluginEngine() {
    prepareEngineInit(1);
    const PlayerErrors e = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    EXPECT(e == noError, "initEngine failed: %d", e);
    return e == noError;
}

Basicwave *waveformOf(unsigned int hash) {
    ActiveSound *sound = player->findByHash(hash);
    if (sound == nullptr || sound->soundType != TYPE_SYNTH)
        return nullptr;
    return static_cast<Basicwave *>(sound->sound.get());
}

// ---- fourier ----------------------------------------------------------------

// Renders [blocks] blocks of a new instance of [wave], as the mixer would,
// and counts the non-finite samples.
int nonFiniteSamplesOf(Basicwave &wave, unsigned int blocks) {
    std::unique_ptr<SoLoud::AudioSourceInstance> instance(wave.createInstance());
    std::vector<float> out(512);
    int nonFinite = 0;
    for (unsigned int b = 0; b < blocks; ++b) {
        instance->getAudio(out.data(), 512, 512);
        for (float s : out)
            if (!std::isfinite(s))
                ++nonFinite;
    }
    return nonFinite;
}

void testFourierTablesStayInBounds() {
    std::printf("fourier: Fourier square and saw lookups stay inside their tables\n");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    // Phases whose period position is 0: the wrap of each must not leave the
    // table, and none has a better answer than the start of the period.
    const struct {
        const char *name;
        float phase;
    } wrapToStart[] = {
        {"-1e-30", -1e-30f},
        {"-2^-26", -1.0f / 67108864.0f},
        {"-FLT_TRUE_MIN", -std::numeric_limits<float>::denorm_min()},
        {"1.0", 1.0f},
        {"1e30", 1e30f},
        {"-1e30", -1e30f},
        {"NaN", nan},
        {"+Inf", inf},
        {"-Inf", -inf},
    };
    for (int waveform : {static_cast<int>(SoLoud::Soloud::WAVE_FSQUARE),
                         static_cast<int>(SoLoud::Soloud::WAVE_FSAW)}) {
        const char *name =
            waveform == SoLoud::Soloud::WAVE_FSQUARE ? "fsquare" : "fsaw";
        const float atStart = SoLoud::Misc::generateWaveform(waveform, 0.0f);
        for (const auto &c : wrapToStart) {
            const float v = SoLoud::Misc::generateWaveform(waveform, c.phase);
            EXPECT(v == atStart, "%s(%s) = %g, expected the period start %g", name,
                   c.name, v, atStart);
        }
        // Ordinary phases, negative ones included, stay in range.
        int outOfRange = 0;
        for (int i = -4096; i <= 4096; ++i) {
            const float v =
                SoLoud::Misc::generateWaveform(waveform, static_cast<float>(i) / 1024.0f);
            if (!(std::fabs(v) <= 1.0f))
                ++outOfRange;
        }
        EXPECT(outOfRange == 0, "%s: %d phases in [-4, 4] gave a value outside [-1, 1]",
               name, outOfRange);
    }

    // A Fourier saw at a tiny negative frequency: its phase starts just
    // below zero.
    {
        Basicwave wave(SoLoud::Soloud::WAVE_FSAW, false, 1.0, 0.0);
        wave.setSamplerate(44100);
        wave.setFreq(-0.0001);
        const int nonFinite = nonFiniteSamplesOf(wave, 4);
        EXPECT(nonFinite == 0, "a Fourier saw at -0.0001 Hz mixed %d non-finite samples",
               nonFinite);
    }
    // A NaN frequency set on the waveform itself (Player::setWaveformFreq
    // refuses one, see `freq`) makes the phase NaN: the mix must survive it.
    for (bool superWave : {false, true}) {
        Basicwave wave(SoLoud::Soloud::WAVE_FSQUARE, superWave, 0.25, 0.5);
        wave.setSamplerate(44100);
        wave.setFreq(nan);
        const int nonFinite = nonFiniteSamplesOf(wave, 4);
        EXPECT(nonFinite == 0, "a Fourier square (superwave %d) at NaN Hz mixed %d "
               "non-finite samples", superWave ? 1 : 0, nonFinite);
    }
}

// ---- freq -------------------------------------------------------------------

void testSetWaveformFreqRefusesNonFinite() {
    std::printf("freq: setWaveformFreq refuses a NaN or infinite frequency\n");
    if (!initPluginEngine())
        return;

    unsigned int hash = 0;
    EXPECT(loadWaveform(SoLoud::Soloud::WAVE_FSQUARE, false, 1.0f, 0.0f, &hash) ==
               noError,
           "loadWaveform should succeed");
    Basicwave *wave = waveformOf(hash);
    EXPECT(wave != nullptr, "the waveform should be registered");
    if (wave == nullptr) {
        dispose();
        return;
    }

    setWaveformFreq(hash, 440.0f);
    EXPECT(static_cast<double>(wave->mFreq) == 440.0, "the frequency should be 440, got %g",
           static_cast<double>(wave->mFreq));
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity()}) {
        setWaveformFreq(hash, bad);
        EXPECT(static_cast<double>(wave->mFreq) == 440.0,
               "setWaveformFreq(%g) changed the frequency to %g", bad,
               static_cast<double>(wave->mFreq));
    }
    // A negative frequency is finite: the oscillator runs backwards.
    setWaveformFreq(hash, -220.0f);
    EXPECT(static_cast<double>(wave->mFreq) == -220.0,
           "the frequency should be -220, got %g", static_cast<double>(wave->mFreq));

    dispose();
}

// ---- params -----------------------------------------------------------------

void testLoadWaveformKeepsScaleAndDetune() {
    std::printf("params: loadWaveform builds the waveform with its scale and detune\n");
    if (!initPluginEngine())
        return;

    const struct {
        bool superWave;
        float scale;
        float detune;
    } cases[] = {{true, 0.25f, 1.0f}, {true, 0.0f, 0.125f}, {false, 0.5f, 0.0f}};
    for (const auto &c : cases) {
        unsigned int hash = 0;
        EXPECT(loadWaveform(SoLoud::Soloud::WAVE_SIN, c.superWave, c.scale, c.detune,
                            &hash) == noError,
               "loadWaveform should succeed");
        Basicwave *wave = waveformOf(hash);
        EXPECT(wave != nullptr, "the waveform should be registered");
        if (wave == nullptr)
            continue;
        const double scale = wave->mSuperwaveScale;
        const double detune = wave->mSuperwaveDetune;
        const bool superWave = wave->mSuperwave;
        EXPECT(scale == static_cast<double>(c.scale), "scale should be %g, got %g",
               c.scale, scale);
        EXPECT(detune == static_cast<double>(c.detune), "detune should be %g, got %g",
               c.detune, detune);
        EXPECT(superWave == c.superWave, "superWave should be %d",
               c.superWave ? 1 : 0);
    }

    dispose();
}

// ---- race -------------------------------------------------------------------

void testSettersWhileTheWaveformPlays() {
    std::printf("race: setWaveform*() while the waveform plays\n");
    if (!initPluginEngine())
        return;

    unsigned int hash = 0;
    EXPECT(loadWaveform(SoLoud::Soloud::WAVE_SIN, true, 0.5f, 0.1f, &hash) == noError,
           "loadWaveform should succeed");
    unsigned int handle = 0;
    EXPECT(play(hash, 0, 0.001f, 0.0f, false, true, 0.0, 0.0, 0, 0, 1.0f, &handle) ==
                   noError &&
               handle != 0,
           "the waveform should play");

    // Let the device start rendering it.
    const double startedAt = getPosition(handle);
    for (int i = 0; i < 100 && getPosition(handle) == startedAt; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT(getPosition(handle) > startedAt, "the device should render the waveform");

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
    unsigned int rounds = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const float f = static_cast<float>(rounds % 100);
        setWaveformFreq(hash, 220.0f + f);
        setWaveformScale(hash, 0.01f * f);
        setWaveformDetune(hash, 0.001f * f);
        setSuperWave(hash, (rounds & 1) != 0);
        setWaveform(hash, static_cast<int>(rounds % 4));
        ++rounds;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    std::printf("  %u rounds of parameter changes\n", rounds);
    // A low bound: a sanitizer reporting a race stalls the loop for a while.
    EXPECT(rounds > 20, "too few parameter changes: %u", rounds);
    EXPECT(getIsValidVoiceHandle(handle) != 0, "the waveform should still play");

    dispose();
}

} // namespace

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : nullptr;
    auto wanted = [&](const char *name) {
        return only == nullptr || std::strcmp(only, name) == 0;
    };

    if (wanted("fourier"))
        testFourierTablesStayInBounds();
    if (wanted("freq"))
        testSetWaveformFreqRefusesNonFinite();
    if (wanted("params"))
        testLoadWaveformKeepsScaleAndDetune();
    if (wanted("race"))
        testSettersWhileTheWaveformPlays();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

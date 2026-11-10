// Standalone native tests for the plugin's filter exports (bindings.cpp) and
// the Filters class behind them (src/filters/filters.cpp).
//
// Scenarios (pass one name to run only that one):
//
//   zero    A filter parameter set to 0, such as the wet level of a fully dry
//           echo, must read back as 0 from a voice and from a bus.
//
//   missing A filter that is not active must report filterNotFound, globally
//           too, and an active one must read back any finite value: 9999 (a
//           parametric EQ band at 9999 Hz) from a voice, a bus, a global
//           filter and a custom filter. A NaN value is filterParameterGetError.
//
//   dispose disposeAllSound() detaches the global filters, so they must no
//           longer be reported as active and must be able to be added again.
//
// Every scenario opens the default output device through the plugin's
// initEngine() (miniaudio falls back to its null backend without one); the
// sounds are waveforms that play paused or at volume 0.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_filter_params_test.sh [--asan|--tsan] [scenario]

#include "enums.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C"
{
    void prepareEngineInit(int64_t owner_engine_id);
    enum PlayerErrors initEngine(int deviceID, unsigned int sampleRate,
                                 unsigned int bufferSize, unsigned int channels,
                                 unsigned int lowLatency,
                                 unsigned int devicePeriodFrames,
                                 unsigned int renderAheadFrames);
    void dispose();
    enum PlayerErrors loadWaveform(int waveform, bool superWave, float scale,
                                   float detune, unsigned int *hash);
    enum PlayerErrors play(unsigned int soundHash, unsigned int busId, float volume,
                           float pan, bool paused, bool looping,
                           double loopingStartAt, double loopingEndAt,
                           int loopingStartOffsetAt, int loopingEndOffsetAt,
                           float scale, unsigned int *handle);
    unsigned int createBus();
    enum PlayerErrors busPlayOnEngine(unsigned int busId, float volume, bool paused,
                                      unsigned int *handle);
    enum PlayerErrors addFilter(unsigned int soundHash, unsigned int busId,
                                enum FilterType filterType);
    enum PlayerErrors setFilterParams(unsigned int handle, unsigned int busId,
                                      enum FilterType filterType, int attributeId,
                                      float value);
    enum PlayerErrors getFilterParams(unsigned int handle, unsigned int busId,
                                      enum FilterType filterType, int attributeId,
                                      float *filterValue);
    enum PlayerErrors addCustomFilter(unsigned int soundHash, unsigned int busId,
                                      int customFilterId);
    enum PlayerErrors setCustomFilterParams(unsigned int handle, unsigned int busId,
                                            int customFilterId, int attributeId,
                                            float value);
    enum PlayerErrors getCustomFilterParams(unsigned int handle, unsigned int busId,
                                            int customFilterId, int attributeId,
                                            float *filterValue);
    void disposeAllSound();
    enum PlayerErrors isFilterActive(unsigned int soundHash, unsigned int busId,
                                     enum FilterType filterType, int *index);
    enum PlayerErrors isCustomFilterActive(unsigned int soundHash,
                                           unsigned int busId,
                                           int customFilterId, int *index);
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

// Echo parameters (soloud_echofilter.h).
constexpr int kEchoWet = 0;
// The parametric EQ's band 0 frequency (parametric_eq_filter.h).
constexpr int kEqBand0Freq = 67;

bool initPluginEngine() {
    prepareEngineInit(1);
    const PlayerErrors e = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    EXPECT(e == noError, "initEngine failed: %d", e);
    return e == noError;
}

// A waveform sound, and a paused voice of it.
unsigned int loadSound() {
    unsigned int hash = 0;
    EXPECT(loadWaveform(2 /* WAVE_SIN */, false, 1.0f, 0.0f, &hash) == noError &&
               hash != 0,
           "loadWaveform should succeed");
    return hash;
}

unsigned int playPaused(unsigned int hash, unsigned int busId = 0) {
    unsigned int handle = 0;
    const PlayerErrors e =
        play(hash, busId, 0.0f, 0.0f, true, true, 0.0, 0.0, 0, 0, 1.0f, &handle);
    EXPECT(e == noError && handle != 0, "play failed: %d", e);
    return handle;
}

unsigned int playBus(unsigned int busId) {
    unsigned int handle = 0;
    const PlayerErrors e = busPlayOnEngine(busId, 0.0f, false, &handle);
    EXPECT(e == noError && handle != 0, "busPlayOnEngine failed: %d", e);
    return handle;
}

// ---- zero -------------------------------------------------------------------

void testZeroParameterReadsBack() {
    std::printf("zero: a parameter set to 0 reads back as 0\n");
    if (!initPluginEngine())
        return;

    const unsigned int hash = loadSound();
    EXPECT(addFilter(hash, 0, EchoFilter) == noError, "addFilter on the sound");
    const unsigned int voice = playPaused(hash);
    EXPECT(setFilterParams(voice, 0, EchoFilter, kEchoWet, 0.0f) == noError,
           "set the voice's echo wet to 0");
    float value = -1.0f;
    PlayerErrors e = getFilterParams(voice, 0, EchoFilter, kEchoWet, &value);
    EXPECT(e == noError && value == 0.0f,
           "the voice's echo wet reads back as error %d, value %g", e, value);

    const unsigned int bus = createBus();
    EXPECT(addFilter(0, bus, EchoFilter) == noError, "addFilter on the bus");
    playBus(bus);
    EXPECT(setFilterParams(0, bus, EchoFilter, kEchoWet, 0.0f) == noError,
           "set the bus's echo wet to 0");
    value = -1.0f;
    e = getFilterParams(0, bus, EchoFilter, kEchoWet, &value);
    EXPECT(e == noError && value == 0.0f,
           "the bus's echo wet reads back as error %d, value %g", e, value);

    // Other values still read back.
    EXPECT(setFilterParams(voice, 0, EchoFilter, kEchoWet, 0.25f) == noError,
           "set the voice's echo wet to 0.25");
    e = getFilterParams(voice, 0, EchoFilter, kEchoWet, &value);
    EXPECT(e == noError && value == 0.25f,
           "the voice's echo wet reads back as error %d, value %g", e, value);

    dispose();
}

// ---- missing ----------------------------------------------------------------

void testNoValueMeansMissing() {
    std::printf("missing: a missing filter is filterNotFound, 9999 reads back\n");
    if (!initPluginEngine())
        return;

    const unsigned int hash = loadSound();
    EXPECT(addFilter(hash, 0, ParametricEQFilter) == noError,
           "addFilter on the sound");
    const unsigned int voice = playPaused(hash);
    EXPECT(setFilterParams(voice, 0, ParametricEQFilter, kEqBand0Freq,
                           9999.0f) == noError,
           "set the voice's band 0 to 9999 Hz");
    float value = -1.0f;
    PlayerErrors e =
        getFilterParams(voice, 0, ParametricEQFilter, kEqBand0Freq, &value);
    EXPECT(e == noError && value == 9999.0f,
           "the voice's band 0 reads back as error %d, value %g", e, value);

    const unsigned int bus = createBus();
    EXPECT(addFilter(0, bus, ParametricEQFilter) == noError,
           "addFilter on the bus");
    playBus(bus);
    EXPECT(setFilterParams(0, bus, ParametricEQFilter, kEqBand0Freq,
                           9999.0f) == noError,
           "set the bus's band 0 to 9999 Hz");
    value = -1.0f;
    e = getFilterParams(0, bus, ParametricEQFilter, kEqBand0Freq, &value);
    EXPECT(e == noError && value == 9999.0f,
           "the bus's band 0 reads back as error %d, value %g", e, value);

    EXPECT(addFilter(0, 0, ParametricEQFilter) == noError,
           "addFilter globally");
    EXPECT(setFilterParams(0, 0, ParametricEQFilter, kEqBand0Freq, 9999.0f) ==
               noError,
           "set the global band 0 to 9999 Hz");
    value = -1.0f;
    e = getFilterParams(0, 0, ParametricEQFilter, kEqBand0Freq, &value);
    EXPECT(e == noError && value == 9999.0f,
           "the global band 0 reads back as error %d, value %g", e, value);

    EXPECT(addCustomFilter(0, 0, 0) == noError, "addCustomFilter globally");
    EXPECT(setCustomFilterParams(0, 0, 0, 1, 9999.0f) == noError,
           "set the global custom filter's parameter 1 to 9999");
    value = -1.0f;
    e = getCustomFilterParams(0, 0, 0, 1, &value);
    EXPECT(e == noError && value == 9999.0f,
           "the custom filter's parameter 1 reads back as error %d, value %g",
           e, value);

    // Filters that were never added.
    e = getFilterParams(0, 0, EchoFilter, kEchoWet, &value);
    EXPECT(e == filterNotFound,
           "a missing global echo reads back as error %d, value %g", e, value);
    e = getFilterParams(voice, 0, EchoFilter, kEchoWet, &value);
    EXPECT(e == filterNotFound,
           "a missing voice echo reads back as error %d, value %g", e, value);
    e = getFilterParams(0, bus, EchoFilter, kEchoWet, &value);
    EXPECT(e == filterNotFound,
           "a missing bus echo reads back as error %d, value %g", e, value);
    e = getCustomFilterParams(0, 0, 1, 1, &value);
    EXPECT(e == filterNotFound,
           "a missing custom filter reads back as error %d, value %g", e,
           value);

    // A NaN value is the one error a present filter reports.
    EXPECT(setFilterParams(0, 0, ParametricEQFilter, kEqBand0Freq, NAN) ==
               noError,
           "set the global band 0 to NaN");
    e = getFilterParams(0, 0, ParametricEQFilter, kEqBand0Freq, &value);
    EXPECT(e == filterParameterGetError,
           "a NaN global parameter reads back as error %d, value %g", e,
           value);
    setFilterParams(0, 0, ParametricEQFilter, kEqBand0Freq, 1000.0f);

    dispose();
}

// ---- dispose ----------------------------------------------------------------

void testDisposeAllSoundForgetsGlobalFilters() {
    std::printf("dispose: disposeAllSound() forgets the global filters\n");
    if (!initPluginEngine())
        return;

    EXPECT(addFilter(0, 0, EchoFilter) == noError, "addFilter globally");
    EXPECT(addCustomFilter(0, 0, 2) == noError, "addCustomFilter globally");
    int index = -2;
    isFilterActive(0, 0, EchoFilter, &index);
    EXPECT(index == 0, "the global echo is at index %d before", index);

    disposeAllSound();

    index = -2;
    isFilterActive(0, 0, EchoFilter, &index);
    EXPECT(index == -1, "the global echo is at index %d after disposeAllSound",
           index);
    index = -2;
    isCustomFilterActive(0, 0, 2, &index);
    EXPECT(index == -1,
           "the global custom filter is at index %d after disposeAllSound",
           index);

    // Added again, the filters work.
    PlayerErrors e = addFilter(0, 0, EchoFilter);
    EXPECT(e == noError, "adding the global echo again gives error %d", e);
    e = addCustomFilter(0, 0, 2);
    EXPECT(e == noError, "adding the global custom filter again gives error %d",
           e);
    EXPECT(setFilterParams(0, 0, EchoFilter, kEchoWet, 0.5f) == noError,
           "set the global echo wet to 0.5");
    float value = -1.0f;
    e = getFilterParams(0, 0, EchoFilter, kEchoWet, &value);
    EXPECT(e == noError && value == 0.5f,
           "the global echo wet reads back as error %d, value %g", e, value);
    index = -2;
    isFilterActive(0, 0, EchoFilter, &index);
    EXPECT(index == 0, "the global echo is at index %d once added again",
           index);

    dispose();
}

} // namespace

int main(int argc, char **argv) {
    const char *only = argc > 1 ? argv[1] : nullptr;
    auto wanted = [&](const char *name) {
        return only == nullptr || std::strcmp(only, name) == 0;
    };

    if (wanted("zero"))
        testZeroParameterReadsBack();
    if (wanted("missing"))
        testNoValueMeansMissing();
    if (wanted("dispose"))
        testDisposeAllSoundForgetsGlobalFilters();

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures == 0 ? 0 : 1;
}

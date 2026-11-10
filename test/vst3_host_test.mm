// Standalone native tests for the VST3 host (src/vst3/vst3_host.cpp and the
// macOS editor window) against a mock plug-in (test/vst3_mock_plugin.cpp).
//
// Build & run from the flutter_soloud repo root (macOS only):
//
//   ./test/run_vst3_host_test.sh [--asan|--tsan] [test name]

#include "vst3/vst3_host.h"
#include "vst3_mock_plugin.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

using SoLoud::Vst3HostManager;

static int g_failures = 0;
static int g_assertions = 0;

#define EXPECT(cond, fmt, ...) do { \
    g_assertions++; \
    if (!(cond)) { \
        g_failures++; \
        std::fprintf(stderr, "  FAIL [%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

namespace {

std::string gBundle;

constexpr float kSampleRate = 44100.0f;

void *userData(int handle) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

// The mock's recorded state, from the image the host loaded for [bundle].
vst3mock::State *mockState(const std::string &bundle) {
    const std::string name = bundle.substr(bundle.find_last_of('/') + 1);
    const std::string exe = bundle + "/Contents/MacOS/" +
                            name.substr(0, name.size() - std::strlen(".vst3"));
    void *image = dlopen(exe.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (image == nullptr)
        return nullptr;
    auto stateFn = reinterpret_cast<vst3mock::StateFn>(dlsym(image, "mockVst3State"));
    dlclose(image);
    return stateFn != nullptr ? stateFn() : nullptr;
}

int loadMock(const std::string &bundle, const char *className) {
    std::vector<SoLoud::Vst3DiscoveredClass> classes;
    Vst3HostManager::instance().scanBundle(bundle.c_str(), classes);
    std::string uid;
    for (const auto &c : classes)
        if (c.name == className)
            uid = c.classUid;
    int handle = 0;
    const PlayerErrors err = Vst3HostManager::instance().loadPlugin(
        bundle.c_str(), uid.c_str(), static_cast<unsigned int>(kSampleRate), 512, 2,
        &handle);
    EXPECT(err == noError && handle > 0, "loading %s from %s failed (error %d)",
           className, bundle.c_str(), static_cast<int>(err));
    if (vst3mock::State *state = mockState(bundle))
        state->reset();
    return handle;
}

void processEffectBlock(int handle, std::vector<float> &buffer, unsigned int frames) {
    vst3FilterDspCallback(buffer.data(), frames, frames, 2, kSampleRate, 0.0, nullptr, 0,
                          0, userData(handle));
}

double presetValue(int paramId) { return (paramId + 1) / 1000.0; }

} // namespace

// A preset of 200 parameters set at once (more than a block's 128 parameter
// queues), and 20 moves of one parameter between two blocks (more than a
// queue's 16 points): every parameter must end at its last value.
static void testParameterChangesReachProcessor() {
    std::printf("Test: parameter changes reach the processor\n");
    const int handle = loadMock(gBundle, "Mock Effect");
    vst3mock::State *state = mockState(gBundle);
    EXPECT(state != nullptr, "no mock state");
    auto plugin = Vst3HostManager::instance().getPlugin(handle);
    if (state == nullptr || plugin == nullptr)
        return;

    constexpr int kPresetParams = 200;
    for (int id = 0; id < kPresetParams; ++id)
        plugin->setParameterNormalized(static_cast<uint32_t>(id), presetValue(id));
    std::vector<float> buffer(2 * 256, 0.0f);
    for (int block = 0; block < 3; ++block)
        processEffectBlock(handle, buffer, 256);
    int arrived = 0;
    for (int id = 0; id < kPresetParams; ++id)
        if (state->lastValue[id].load() == presetValue(id))
            ++arrived;
    EXPECT(arrived == kPresetParams, "%d of %d preset parameters reached the processor",
           arrived, kPresetParams);
    EXPECT(state->maxQueuesInBlock.load() <= 128, "a block carried %d parameter queues",
           state->maxQueuesInBlock.load());

    constexpr uint32_t kMovedParam = 250;
    for (int step = 1; step <= 20; ++step)
        plugin->setParameterNormalized(kMovedParam, step / 20.0);
    processEffectBlock(handle, buffer, 256);
    EXPECT(state->lastValue[kMovedParam].load() == 1.0,
           "the moved parameter ended at %.2f, not at its last value 1.00",
           state->lastValue[kMovedParam].load());

    plugin.reset();
    Vst3HostManager::instance().unloadPlugin(handle);
}

// A block the instrument fails to render must come out silent, whether the
// caller's buffer is aligned for the plug-in (it renders into it directly)
// or not (it renders into scratch memory). So must a channel the instrument
// does not render.
static void testInstrumentOutputIsNeverStale() {
    std::printf("Test: instrument output is never stale\n");
    const int handle = loadMock(gBundle, "Mock Instrument");
    vst3mock::State *state = mockState(gBundle);
    EXPECT(state != nullptr, "no mock state");
    if (state == nullptr || handle <= 0)
        return;

    constexpr unsigned int kFrames = 256;
    float *storage =
        static_cast<float *>(std::aligned_alloc(64, sizeof(float) * (3 * kFrames + 64)));
    // Renders one block into a buffer holding 1.0, starting [firstFloat]
    // floats into the 64-byte aligned storage.
    auto render = [&](unsigned int firstFloat, unsigned int channels) {
        float *buffer = storage + firstFloat;
        std::fill(buffer, buffer + channels * kFrames, 1.0f);
        uint32_t ended = 0;
        const unsigned int frames =
            vst3SourceDspCallback(buffer, kFrames, kFrames, channels, kSampleRate, 0.0,
                                  0.0, &ended, userData(handle));
        EXPECT(frames == kFrames, "rendered %u of %u frames", frames, kFrames);
        return buffer;
    };
    auto allEqual = [](const float *samples, unsigned int count, float value) {
        for (unsigned int i = 0; i < count; ++i)
            if (samples[i] != value)
                return false;
        return true;
    };

    const float *out = render(0, 2);
    EXPECT(allEqual(out, 2 * kFrames, vst3mock::kInstrumentLevel),
           "a rendered block holds %g, not the instrument's level", out[0]);

    state->failProcess.store(true);
    out = render(0, 2);
    EXPECT(allEqual(out, 2 * kFrames, 0.0f),
           "an aligned buffer holds %g after a failed block, not silence", out[0]);
    out = render(1, 2);
    EXPECT(allEqual(out, 2 * kFrames, 0.0f),
           "an unaligned buffer holds %g after a failed block, not silence", out[0]);
    state->failProcess.store(false);

    out = render(0, 3);
    EXPECT(allEqual(out, 2 * kFrames, vst3mock::kInstrumentLevel),
           "a three-channel block holds %g, not the instrument's level", out[0]);
    EXPECT(allEqual(out + 2 * kFrames, kFrames, 0.0f),
           "the third channel of a stereo instrument holds %g, not silence",
           out[2 * kFrames]);

    std::free(storage);
    Vst3HostManager::instance().unloadPlugin(handle);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <directory of the mock bundles> [test]\n", argv[0]);
        return 2;
    }
    gBundle = std::string(argv[1]) + "/MockVst3.vst3";
    const std::string only = argc > 2 ? argv[2] : "";
    const std::pair<const char *, void (*)()> tests[] = {
        {"params", testParameterChangesReachProcessor},
        {"instrument", testInstrumentOutputIsNeverStale},
    };
    for (const auto &test : tests)
        if (only.empty() || only == test.first)
            test.second();

    std::printf("\n%d assertions, %d failures\n", g_assertions, g_failures);
    return g_failures == 0 ? 0 : 1;
}

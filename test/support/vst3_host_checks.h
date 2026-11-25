// The VST3 host tests (src/vst3/vst3_host.cpp and the editor windows) against
// the mock plug-in in test/support/vst3_mock_plugin.cpp, shared by
//   test/common/vst3_host_test.cpp  (Linux, Windows; built by
//                                    test/linux/vst3_host_test.sh and
//                                    test/windows/vst3_host_test.ps1)
//   test/macos/vst3_host_test.mm    (macOS, test/macos/vst3_host_test.sh)
// which add their own main() and the exit-time unload trigger.
//
// The scripts lay out two mock bundles in a work directory:
//   MockVst3.vst3        the plug-in alone;
//   MockVst3Remote.vst3  the same binary plus the marker of an embedded
//                        Flutter app (macOS Contents/Frameworks/App.framework,
//                        Linux Contents/<arch>-linux/data/flutter_assets,
//                        Windows Contents\<arch>-win\data\flutter_assets), so
//                        the host opens its editor out of process in
//                        SOLOUD_VST3_HELPER_PATH, which the scripts point at
//                        test/support/vst3_mock_editor_helper.cpp.
//
// Platform specifics are confined to three helpers below: where a bundle's
// binary is (mockState), how the editor's poll timer runs (pumpMainLoop), and
// aligned allocation.

#ifndef VST3_HOST_CHECKS_H
#define VST3_HOST_CHECKS_H

#include "vst3/vst3_host.h"
#include "vst3_mock_plugin.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <malloc.h>
#else
#include <dlfcn.h>
#endif
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

using SoLoud::Vst3HostManager;
using SoLoud::Vst3PluginInstance;

namespace vst3test {

inline int &failures() { static int n = 0; return n; }
inline int &assertions() { static int n = 0; return n; }

#define EXPECT(cond, fmt, ...) do { \
    vst3test::assertions()++; \
    if (!(cond)) { \
        vst3test::failures()++; \
        std::fprintf(stderr, "  FAIL [%s:%d] " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

inline std::string &bundlePath() { static std::string s; return s; }
// The same plug-in in a bundle the host opens in the out-of-process editor
// helper (here test/support/vst3_mock_editor_helper.cpp).
inline std::string &remoteBundlePath() { static std::string s; return s; }

constexpr float kSampleRate = 44100.0f;

inline void *userData(int handle) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

// ---------------------------------------------------------------------------
// Platform helpers.

/// The mock's binary inside [bundle], as the VST3 SDK's module loader finds it.
inline std::string mockBinary(const std::string &bundle) {
    const size_t slash = bundle.find_last_of("/\\");
    const std::string name = bundle.substr(slash + 1);
    const std::string base = name.substr(0, name.size() - std::strlen(".vst3"));
#if defined(__APPLE__)
    return bundle + "/Contents/MacOS/" + base;
#elif defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    return bundle + "\\Contents\\arm64-win\\" + base + ".vst3";
#else
    return bundle + "\\Contents\\x86_64-win\\" + base + ".vst3";
#endif
#else
#if defined(__aarch64__)
    return bundle + "/Contents/aarch64-linux/" + base + ".so";
#else
    return bundle + "/Contents/x86_64-linux/" + base + ".so";
#endif
#endif
}

/// The mock's recorded state, from the image the host loaded for [bundle].
inline vst3mock::State *mockState(const std::string &bundle) {
    const std::string exe = mockBinary(bundle);
#if defined(_WIN32)
    HMODULE image = GetModuleHandleA(exe.c_str());
    if (image == nullptr)
        return nullptr;
    auto stateFn = reinterpret_cast<vst3mock::StateFn>(
        reinterpret_cast<void *>(GetProcAddress(image, "mockVst3State")));
#else
    void *image = dlopen(exe.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (image == nullptr)
        return nullptr;
    auto stateFn = reinterpret_cast<vst3mock::StateFn>(dlsym(image, "mockVst3State"));
    dlclose(image);
#endif
    return stateFn != nullptr ? stateFn() : nullptr;
}

/// Runs one round of the main loop the editor's poll timer lives on.
inline void pumpMainLoop() {
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.002, true);
#elif defined(_WIN32)
    // SetTimer(nullptr, ...) timers are delivered as WM_TIMER to this thread.
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Sleep(2);
#else
    // The Linux editor's timers are g_timeout_add() sources on GLib's default
    // context, loaded at run time as the editor itself does.
    using Iterate = int (*)(void *, int);
    static Iterate iterate = [] {
        void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
        return glib ? reinterpret_cast<Iterate>(dlsym(glib, "g_main_context_iteration")) : nullptr;
    }();
    if (iterate != nullptr) {
        while (iterate(nullptr, 0)) {
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
#endif
}

/// Whether pumpMainLoop() can run the editor's timers here.
inline bool canPumpEditorTimers() {
#if defined(__APPLE__) || defined(_WIN32)
    return true;
#else
    void *glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
    if (glib == nullptr) glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    return glib != nullptr;
#endif
}

inline float *alignedAlloc(size_t alignment, size_t bytes) {
#if defined(_WIN32)
    return static_cast<float *>(_aligned_malloc(bytes, alignment));
#else
    // aligned_alloc needs the size to be a multiple of the alignment.
    return static_cast<float *>(std::aligned_alloc(alignment, (bytes + alignment - 1) / alignment * alignment));
#endif
}

inline void alignedFree(void *p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

// ---------------------------------------------------------------------------
// Shared helpers.

inline int loadMock(const std::string &bundle, const char *className) {
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

inline void processEffectBlock(int handle, std::vector<float> &buffer, unsigned int frames) {
    vst3FilterDspCallback(buffer.data(), frames, frames, 2, kSampleRate, 0.0, nullptr, 0,
                          0, userData(handle));
}

inline bool waitFor(const std::function<bool()> &done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        pumpMainLoop();
    }
    return true;
}

// Runs an effect block after block the way a filter slot does, until stopped.
class EffectAudioThread {
public:
    explicit EffectAudioThread(int handle) : mHandle(handle), mThread([this] { run(); }) {}
    ~EffectAudioThread() { stop(); }

    void stop() {
        mStop.store(true);
        if (mThread.joinable())
            mThread.join();
    }
    long blocks() const { return mBlocks.load(); }

private:
    void run() {
        constexpr unsigned int kFrames = 64;
        std::vector<float> buffer(2 * kFrames, 0.5f);
        while (!mStop.load()) {
            processEffectBlock(mHandle, buffer, kFrames);
            mBlocks.fetch_add(1);
        }
    }

    const int mHandle;
    std::atomic<bool> mStop{false};
    std::atomic<long> mBlocks{0};
    std::thread mThread;
};

inline double presetValue(int paramId) { return (paramId + 1) / 1000.0; }

// ---------------------------------------------------------------------------
// Tests.

// A preset of 200 parameters set at once (more than a block's 128 parameter
// queues), and 20 moves of one parameter between two blocks (more than a
// queue's 16 points): every parameter must end at its last value.
inline void testParameterChangesReachProcessor() {
    std::printf("Test: parameter changes reach the processor\n");
    const int handle = loadMock(bundlePath(), "Mock Effect");
    vst3mock::State *state = mockState(bundlePath());
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
inline void testInstrumentOutputIsNeverStale() {
    std::printf("Test: instrument output is never stale\n");
    const int handle = loadMock(bundlePath(), "Mock Instrument");
    vst3mock::State *state = mockState(bundlePath());
    EXPECT(state != nullptr, "no mock state");
    if (state == nullptr || handle <= 0)
        return;

    constexpr unsigned int kFrames = 256;
    float *storage = alignedAlloc(64, sizeof(float) * (3 * kFrames + 64));
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

    alignedFree(storage);
    Vst3HostManager::instance().unloadPlugin(handle);
}

// Opening and closing the editor while the effect processes. The mock has no
// editor view, so each open stops at the editor window object, which the
// audio thread reads every block. Run under --tsan / --asan.
inline void testEditorOpenCloseWhileProcessing() {
    std::printf("Test: editor open/close while the effect processes\n");
    const int handle = loadMock(bundlePath(), "Mock Effect");
    auto plugin = Vst3HostManager::instance().getPlugin(handle);
    if (plugin == nullptr)
        return;

    EffectAudioThread audio(handle);
    EXPECT(waitFor([&] { return audio.blocks() > 10; }, std::chrono::seconds(5)),
           "the audio thread did not start");
    for (int i = 0; i < 2000; ++i) {
        plugin->openEditor("Mock Effect");
        plugin->closeEditor();
    }
    const long blocks = audio.blocks();
    audio.stop();
    EXPECT(blocks > 20, "only %ld blocks ran during the open/close loop", blocks);

    plugin.reset();
    Vst3HostManager::instance().unloadPlugin(handle);
}

// Closing an out-of-process editor while the effect writes telemetry into
// the shared memory it maps: from the host (closeEditor), and from the
// helper (the user closed its window, which the host's poll timer notices).
// Run under --tsan / --asan.
inline void testRemoteEditorCloseWhileProcessing() {
    std::printf("Test: remote editor close while the effect processes\n");
    if (!canPumpEditorTimers()) {
        std::printf("  not run: no GLib main loop to run the editor's poll timer\n");
        return;
    }
    const int handle = loadMock(remoteBundlePath(), "Mock Effect");
    auto plugin = Vst3HostManager::instance().getPlugin(handle);
    if (plugin == nullptr)
        return;

    EffectAudioThread audio(handle);
    for (int i = 0; i < 60; ++i) {
        const bool helperCloses = (i % 2) == 1;
        if (!plugin->openEditor(helperCloses ? "helper-closes" : "Mock Effect")) {
            EXPECT(false, "remote editor %d did not open", i);
            break;
        }
        EXPECT(plugin->isEditorOpen(), "remote editor %d is not open", i);
        // Let the audio thread write telemetry for a while. (Reading its
        // block count here would order those writes before the close for
        // ThreadSanitizer.)
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        if (helperCloses) {
            EXPECT(waitFor([&] { return !plugin->isEditorOpen(); }, std::chrono::seconds(2)),
                   "the host did not notice that helper %d closed", i);
        } else {
            plugin->closeEditor();
        }
    }
    const long blocks = audio.blocks();
    audio.stop();
    EXPECT(blocks > 20, "only %ld blocks ran while the editors were open", blocks);

    plugin.reset();
    Vst3HostManager::instance().unloadPlugin(handle);
}

// The unload the app runs when it exits must wait for a mix that took the
// plugin's pointer from the lookup table before it destroys the plugin.
// [triggerExitUnload] runs that unload the way the platform does (macOS: the
// NSApplicationWillTerminateNotification observer; elsewhere
// Vst3HostManager::unloadAllAtExit(), what the manager's destructor calls).
inline void testExitUnloadWaitsForAudioThread(const std::function<void()> &triggerExitUnload) {
    std::printf("Test: exit-time unload waits for the audio thread\n");
    // Held by every mix, as SoLoud's audio mutex; the barrier waits for it.
    static std::mutex audioMutex;
    static void (*barrier)(void *) = [](void *) { std::lock_guard<std::mutex> wait(audioMutex); };
    Vst3HostManager::instance().setAudioThreadBarrier(barrier, nullptr);
    const int handle = loadMock(bundlePath(), "Mock Effect");

    std::atomic<bool> pointerTaken{false};
    std::atomic<bool> unloadReturned{false};
    std::atomic<bool> processed{false};
    std::thread mix([&] {
        std::lock_guard<std::mutex> mixing(audioMutex);
        // vst3FilterDspCallback, preempted between its lookup and the call.
        Vst3PluginInstance *plugin = Vst3HostManager::instance().getPluginAudioThread(handle);
        pointerTaken.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // Once the unload has returned the plugin is gone: don't touch it.
        if (plugin != nullptr && !unloadReturned.load()) {
            std::vector<float> buffer(2 * 64, 0.5f);
            plugin->processAudioEffectBlock(buffer.data(), 64, 64, 2, kSampleRate, 0.0,
                                            nullptr, 0, 0);
            processed.store(true);
        }
    });
    while (!pointerTaken.load())
        std::this_thread::yield();
    triggerExitUnload();
    unloadReturned.store(true);
    mix.join();

    EXPECT(processed.load(), "the exit-time unload destroyed the plugin under a running mix");
    EXPECT(Vst3HostManager::instance().getPlugin(handle) == nullptr,
           "the exit-time unload left the plugin loaded");
    Vst3HostManager::instance().setAudioThreadBarrier(nullptr, nullptr);
}

/// Runs the tests named in argv[2] (or all), with the bundles in argv[1].
inline int run(int argc, char **argv, const std::function<void()> &triggerExitUnload) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <directory of the mock bundles> [test]\n", argv[0]);
        return 2;
    }
#if defined(_WIN32)
    const char *sep = "\\";
#else
    const char *sep = "/";
#endif
    bundlePath() = std::string(argv[1]) + sep + "MockVst3.vst3";
    remoteBundlePath() = std::string(argv[1]) + sep + "MockVst3Remote.vst3";
    const std::string only = argc > 2 ? argv[2] : "";
    const std::pair<const char *, std::function<void()>> tests[] = {
        {"params", testParameterChangesReachProcessor},
        {"instrument", testInstrumentOutputIsNeverStale},
        {"editor", testEditorOpenCloseWhileProcessing},
        {"remote-editor", testRemoteEditorCloseWhileProcessing},
        // Last: it unloads every plugin, as when the app terminates.
        {"exit", [&] { testExitUnloadWaitsForAudioThread(triggerExitUnload); }},
    };
    for (const auto &test : tests)
        if (only.empty() || only == test.first)
            test.second();

    std::printf("\n%d assertions, %d failures\n", assertions(), failures());
    return failures() == 0 ? 0 : 1;
}

} // namespace vst3test

#endif // VST3_HOST_CHECKS_H

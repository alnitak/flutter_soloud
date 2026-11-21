// Android backend choice by API level: init_platform_context()
// (src/soloud/src/backend/miniaudio/soloud_miniaudio.cpp) opens OpenSL ES
// alone on Android <= 10 (API <= 29), where uninitializing an AAudio device
// can crash, and AAudio first (OpenSL as the fallback) from API 30.
//
// The device's API level comes from android_get_device_api_level(), which
// this executable defines itself so the engine sees whatever level a case
// sets (built for API >= 29 so the NDK header declares the libc function
// instead of inlining a property read). For each level the test drives the
// FFI entry points Dart uses and checks:
//   * the context's backend (OpenSL for 21..29, AAudio for 30+);
//   * a voice really mixes (its stream time advances with the wall clock);
//   * the device state and the started/stopped events Dart receives;
//   * stopAudioDevice/startAudioDevice and changeDevice on that backend;
//   * repeated init/deinit, alternating backends, with low latency on and off;
//   * a deinit with a voice playing, and init after a deinit on the other one.
//
// Build & run with test/android/opensl_fallback_test.sh (needs a device or
// emulator: it opens the real output).

#include "../support/engine_ffi.h"

#include "soloud.h"
#include "backend/miniaudio/miniaudio.h"

using namespace enginetest;

static std::atomic<int> gFakeApiLevel{34};
static std::atomic<int> gApiLevelQueries{0};

extern "C" int android_get_device_api_level() {
    gApiLevelQueries++;
    return gFakeApiLevel.load();
}

namespace SoLoud {
extern ma_context context;
extern ma_device gDevice;
}

static const char *backendName(ma_backend b) {
    switch (b) {
    case ma_backend_aaudio: return "AAudio";
    case ma_backend_opensl: return "OpenSL";
    case ma_backend_null: return "null";
    default: return "other";
    }
}

static void runLevel(int api, bool lowLatency) {
    std::printf("[case] API %d, lowLatency %d\n", api, lowLatency ? 1 : 0);
    gFakeApiLevel = api;
    gState.clear();
    const int queriesBefore = gApiLevelQueries.load();
    const PlayerErrors e = initLikeDart(lowLatency);
    ET_CHECK(e == noError, "init returns noError (%d)", static_cast<int>(e));
    if (e != noError) return;
    ET_CHECK(gApiLevelQueries.load() > queriesBefore, "init asked for the device API level");
    const ma_backend expected = api <= 29 ? ma_backend_opensl : ma_backend_aaudio;
    ET_CHECK(SoLoud::context.backend == expected, "backend is %s (expected %s)", backendName(SoLoud::context.backend),
             backendName(expected));

    const unsigned int h = playSine();
    ET_CHECK(h != 0, "play a sine");
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device started after play (state %d)",
             static_cast<int>(getAudioDeviceState()));
    const double adv = streamAdvance(h, 500);
    ET_CHECK(adv > 0.25, "the voice mixes: stream time advanced %.3f s in 0.5 s", adv);
    ET_CHECK(gState.waitFor(event_started), "Dart received 'started' (%s)", gState.describe().c_str());

    // Forced stop and restart on this backend.
    ET_CHECK(stopAudioDevice(1) == noError, "stopAudioDevice(force)");
    ET_CHECK(waitDeviceState(audioDeviceStopped), "device stopped");
    const double stopped = streamAdvance(h, 300);
    ET_CHECK(stopped < 0.05, "nothing mixes while stopped (%.3f s)", stopped);
    ET_CHECK(startAudioDevice() == noError, "startAudioDevice");
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device started again");
    ET_CHECK(mixing(h), "the voice mixes again after the restart");

    // A device change rebuilds the device on the same context.
    ET_CHECK(changeDevice(-1) == noError, "changeDevice(default)");
    ET_CHECK(SoLoud::context.backend == expected, "backend after changeDevice is %s",
             backendName(SoLoud::context.backend));
    ET_CHECK(mixing(h, 700), "the voice mixes after changeDevice");

    // Deinit with the voice still playing.
    dispose();
    ET_CHECK(!isInited(), "deinit with a voice playing");
    ET_CHECK(getAudioDeviceState() == audioDeviceUninitialized, "device uninitialized after deinit (state %d)",
             static_cast<int>(getAudioDeviceState()));
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    for (int api : {21, 26, 28, 29}) runLevel(api, false);
    for (int api : {30, 34}) runLevel(api, false);
    runLevel(29, true);
    runLevel(31, true);

    // Repeated init/deinit alternating the backends.
    std::printf("[case] 10 init/deinit cycles alternating API 29 / 30\n");
    int ok = 0;
    for (int i = 0; i < 10; ++i) {
        const int api = i % 2 ? 30 : 29;
        gFakeApiLevel = api;
        if (initLikeDart(i % 3 == 0) != noError) continue;
        const unsigned int h = playSine();
        const bool good = h != 0 &&
                          SoLoud::context.backend == (api <= 29 ? ma_backend_opensl : ma_backend_aaudio) &&
                          mixing(h, 300);
        dispose();
        ok += good;
    }
    ET_CHECK(ok == 10, "%d/10 cycles opened the right backend and mixed", ok);

    // Init and deinit with nothing played, quickly.
    std::printf("[case] 20 quick init/deinit cycles on OpenSL without playing\n");
    gFakeApiLevel = 28;
    int quick = 0;
    for (int i = 0; i < 20; ++i) {
        quick += initLikeDart() == noError;
        dispose();
    }
    ET_CHECK(quick == 20, "%d/20 quick cycles initialized", quick);

    return finish("OpenSL fallback");
}

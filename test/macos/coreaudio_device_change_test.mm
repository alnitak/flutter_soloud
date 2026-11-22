// CoreAudio output-device changes on macOS while the engine plays, end to end
// through the FFI entry points Dart uses and the real CoreAudio backend
// (miniaudio's default-device tracking in soloud_miniaudio.cpp's backend).
//
// The test creates a process-private aggregate device (over BlackHole when
// it is installed, else over the current default output), so nothing about
// the Mac's audio setup is changed by default:
//   * the engine opened on that device plays (its voice mixes);
//   * the device is removed while in use (the aggregate is destroyed): the
//     process neither crashes nor hangs, Dart is told, and the engine plays
//     again on the default output (by itself, or after changeDevice(-1));
//   * changeDevice() between the aggregate and the default output, and back.
// With SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT=1 it also switches the system
// default output while the engine plays on "default": to the aggregate, then
// removes the aggregate while it is the default (macOS falls back to another
// device), checking each time that the engine follows (rerouted, mixing).
// The original default output is restored afterwards, also on failure or a
// crash (test/support/coreaudio_test_devices.h).
//
// Build & run with test/macos/coreaudio_device_change_test.sh.

#import <Foundation/Foundation.h>

#include <cstdlib>

#include "../support/coreaudio_test_devices.h"
#include "../support/engine_ffi.h"

using namespace enginetest;

static PlayerErrors initOn(int deviceId) {
    prepareEngineInit(-1);
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, -1);
    return initEngine(deviceId, 44100, 2048, 2, 0, 0, 0);
}

static int waitEngineDevice(const char *part, int ms = 3000) {
    for (int waited = 0; waited < ms; waited += 50) {
        const int id = findEngineDevice(part);
        if (id != -2) return id;
        sleepMs(50);
    }
    return -2;
}

static void removalCase(const std::string &subUID) {
    std::printf("[case] the device in use is removed\n");
    AudioObjectID agg = catest::createPrivateAggregate("SoLoud Test Aggregate", "dev.flutter-soloud.test.agg1", subUID);
    ET_CHECK(agg != kAudioObjectUnknown, "private aggregate device created");
    if (agg == kAudioObjectUnknown) return;
    const int engineId = waitEngineDevice("SoLoud Test Aggregate");
    ET_CHECK(engineId != -2, "the engine lists it (id %d)", engineId);
    if (engineId == -2) {
        catest::destroyAggregate(agg);
        return;
    }
    gState.clear();
    ET_CHECK(initOn(engineId) == noError, "init on the aggregate");
    const unsigned int h = playSine();
    ET_CHECK(h != 0, "sound started");
    ET_CHECK(waitDeviceState(audioDeviceStarted, 5000), "device started (state %d)", static_cast<int>(getAudioDeviceState()));
    // A freshly created aggregate can take a moment to start its clock (slow on CI runners).
    bool playing = false;
    for (int i = 0; i < 5 && !playing; ++i) playing = mixing(h, 500);
    if (!playing && std::getenv("CI") != nullptr) {
        // GitHub's macOS runners have no audio hardware: an aggregate built
        // over their virtual output starts but its clock never runs when the
        // engine opens it directly (changeDevice to one works, see the next
        // case). The rest of this case, the removal, still runs.
        std::printf("  note: the aggregate does not run on this machine (CI), not counted\n");
    } else {
        ET_CHECK(playing, "playing on the aggregate");
    }

    const auto t0 = std::chrono::steady_clock::now();
    ET_CHECK(catest::destroyAggregate(agg), "aggregate destroyed while in use");
    sleepMs(1500);
    std::printf("  events after the removal: %s; device state %d\n", gState.describe().c_str(),
                static_cast<int>(getAudioDeviceState()));
    const double adv = streamAdvance(h, 700);
    std::printf("  stream time advanced %.3f s in 0.7 s after the removal\n", adv);
    ET_CHECK(isInited(), "still initialized, no crash");
    const bool followed = adv > 0.35;
    if (!followed) {
        // Not followed by itself: what an app does on 'stopped'/'rerouted'.
        ET_CHECK(gState.count(event_stopped) + gState.count(event_rerouted) > 0,
                 "Dart was told the device went away (%s)", gState.describe().c_str());
        ET_CHECK(changeDevice(-1) == noError, "changeDevice(default) after the removal");
    }
    ET_CHECK(waitDeviceState(audioDeviceStarted) && mixing(h, 700), "plays on the default output %s",
             followed ? "(followed by itself)" : "after changeDevice");
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    ET_CHECK(secs < 10, "recovered in %.1f s", secs);
    dispose();
    ET_CHECK(!isInited(), "deinit");
}

static void changeDeviceCase(const std::string &subUID) {
    std::printf("[case] changeDevice between the aggregate and the default output\n");
    AudioObjectID agg = catest::createPrivateAggregate("SoLoud Test Aggregate 2", "dev.flutter-soloud.test.agg2", subUID);
    const int engineId = agg ? waitEngineDevice("SoLoud Test Aggregate 2") : -2;
    ET_CHECK(engineId != -2, "aggregate listed");
    gState.clear();
    ET_CHECK(initOn(-1) == noError, "init on the default output");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && mixing(h), "playing");
    for (int i = 0; i < 3 && engineId != -2; ++i) {
        ET_CHECK(changeDevice(engineId) == noError && mixing(h, 400), "to the aggregate (%d)", i);
        ET_CHECK(changeDevice(-1) == noError && mixing(h, 400), "back to the default (%d)", i);
    }
    dispose();
    if (agg) catest::destroyAggregate(agg);
}

static void defaultOutputCase(const std::string &subUID) {
    std::printf("[case] the system default output changes (SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT=1)\n");
    const AudioObjectID original = catest::defaultOutput();
    AudioObjectID agg = catest::createPrivateAggregate("SoLoud Test Aggregate 3", "dev.flutter-soloud.test.agg3", subUID);
    ET_CHECK(agg != kAudioObjectUnknown, "aggregate created");
    if (!agg) return;
    gState.clear();
    ET_CHECK(initOn(-1) == noError, "init on the default output");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && mixing(h), "playing");

    ET_CHECK(catest::setDefaultOutput(agg), "system default -> aggregate");
    ET_CHECK(gState.waitFor(event_rerouted, 1, 3000), "Dart received rerouted (%s)", gState.describe().c_str());
    ET_CHECK(mixing(h, 700), "plays on the new default");

    ET_CHECK(catest::destroyAggregate(agg), "the default device (aggregate) is removed");
    sleepMs(1000);
    std::printf("  macOS picked %s as the default\n", catest::deviceName(catest::defaultOutput()).c_str());
    ET_CHECK(gState.waitFor(event_rerouted, 2, 3000), "rerouted again (%s)", gState.describe().c_str());
    ET_CHECK(mixing(h, 700), "plays on the fallback default");

    catest::SystemAudioRestorer::instance().restore();
    ET_CHECK(catest::defaultOutput() == original, "original default output restored (%s)",
             catest::deviceName(catest::defaultOutput()).c_str());
    sleepMs(800);
    ET_CHECK(mixing(h, 700), "plays after the restore");
    dispose();
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    @autoreleasepool {
        catest::SystemAudioRestorer::instance(); // handlers installed first
        AudioObjectID sub = catest::findOutputByName("BlackHole");
        if (sub == kAudioObjectUnknown) sub = catest::defaultOutput();
        if (sub == kAudioObjectUnknown) {
            std::printf("SKIPPED: no output device\n");
            return 0;
        }
        const std::string subUID = catest::deviceUID(sub);
        std::printf("aggregates are built over: %s\n", catest::deviceName(sub).c_str());
        removalCase(subUID);
        changeDeviceCase(subUID);
        const char *env = std::getenv("SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT");
        if (env && std::strcmp(env, "1") == 0) {
            defaultOutputCase(subUID);
        } else {
            std::printf("[case] system default output change: not run (set SOLOUD_TEST_CHANGE_DEFAULT_OUTPUT=1; "
                        "it switches the Mac's default output for a few seconds and restores it)\n");
        }
        catest::SystemAudioRestorer::instance().restore();
    }
    return finish("CoreAudio device change");
}

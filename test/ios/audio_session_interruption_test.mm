// AVAudioSession interruptions and route changes on iOS, end to end through
// the engine: miniaudio's ma_ios_notification_handler (compiled through
// src/soloud_miniaudio_objc.mm), the engine's own interruption/lifecycle
// observers in soloud_miniaudio.cpp, Player::handleAudioInterruption and the
// state events Dart receives.
//
// Runs in the iOS simulator against the real CoreAudio output. The
// notifications are posted on NSNotificationCenter with the payloads iOS
// uses (object = [AVAudioSession sharedInstance]):
//   * interruption began: the device stops, Dart gets interruption_began,
//     a play during the interruption does not restart the device;
//   * interruption ended with AVAudioSessionInterruptionOptionShouldResume:
//     Dart gets interruption_ended, the device restarts and the voice mixes;
//   * interruption ended without shouldResume: the same (the engine resumes
//     active playback either way);
//   * ended with an idle engine (finite idle timeout): the device stays
//     stopped until the next play;
//   * route change (old device unavailable, new device, category change):
//     Dart gets rerouted, playback continues;
//   * media services were reset: playback continues (nothing in the engine
//     observes it; see the note printed);
//   * deinit during an interruption, and init afterwards.
//
// The main thread runs the run loop (the engine hops to the main queue);
// the checks run on a worker thread.
//
// Build & run with test/ios/audio_session_interruption_test.sh.

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "../support/engine_ffi.h"

using namespace enginetest;

static void post(NSString *name, NSDictionary *userInfo) {
    @autoreleasepool {
        [[NSNotificationCenter defaultCenter] postNotificationName:name
                                                            object:[AVAudioSession sharedInstance]
                                                          userInfo:userInfo];
    }
}

static void interruptionBegan() {
    post(AVAudioSessionInterruptionNotification,
         @{AVAudioSessionInterruptionTypeKey : @(AVAudioSessionInterruptionTypeBegan)});
}

static void interruptionEnded(bool shouldResume) {
    post(AVAudioSessionInterruptionNotification, @{
        AVAudioSessionInterruptionTypeKey : @(AVAudioSessionInterruptionTypeEnded),
        AVAudioSessionInterruptionOptionKey : @(shouldResume ? AVAudioSessionInterruptionOptionShouldResume : 0)
    });
}

static void routeChange(AVAudioSessionRouteChangeReason reason) {
    post(AVAudioSessionRouteChangeNotification, @{AVAudioSessionRouteChangeReasonKey : @(reason)});
}

static void endedCase(bool shouldResume) {
    std::printf("[case] interruption began, then ended %s shouldResume\n", shouldResume ? "with" : "without");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing (state %d)",
             static_cast<int>(getAudioDeviceState()));
    ET_CHECK(mixing(h), "mixing before the interruption");

    interruptionBegan();
    ET_CHECK(gState.waitFor(event_interruption_began), "Dart received interruption_began (%s)",
             gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStopped), "device stopped (state %d)",
             static_cast<int>(getAudioDeviceState()));
    const double during = streamAdvance(h, 400);
    ET_CHECK(during < 0.05, "nothing mixes during the interruption (%.3f s)", during);

    // A new play during the interruption must not restart the device.
    const unsigned int h2 = playSine(0.02f);
    sleepMs(400);
    ET_CHECK(h2 != 0 && getAudioDeviceState() == audioDeviceStopped,
             "play during the interruption leaves the device stopped (state %d)",
             static_cast<int>(getAudioDeviceState()));

    interruptionEnded(shouldResume);
    ET_CHECK(gState.waitFor(event_interruption_ended), "Dart received interruption_ended (%s)",
             gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device restarted (state %d)",
             static_cast<int>(getAudioDeviceState()));
    const double after = streamAdvance(h, 700);
    ET_CHECK(after > 0.35, "the voice mixes again (%.3f s in 0.7 s)", after);
    ET_CHECK(getIsValidVoiceHandle(h) && getIsValidVoiceHandle(h2), "both voices survived");
    dispose();
}

static void idleEndedCase() {
    std::printf("[case] interruption ended with an idle engine\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    setAudioDeviceIdleTimeout(200);
    unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    interruptionBegan();
    ET_CHECK(gState.waitFor(event_interruption_began) && waitDeviceState(audioDeviceStopped),
             "interrupted and stopped");
    stop(h);
    interruptionEnded(true);
    ET_CHECK(gState.waitFor(event_interruption_ended), "Dart received interruption_ended");
    sleepMs(800);
    ET_CHECK(getAudioDeviceState() == audioDeviceStopped, "no voice: the device stays stopped (state %d)",
             static_cast<int>(getAudioDeviceState()));
    h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted) && mixing(h), "the next play starts it and mixes");
    setAudioDeviceIdleTimeout(-1);
    dispose();
}

static void routeCase() {
    std::printf("[case] route changes\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    const AVAudioSessionRouteChangeReason reasons[] = {AVAudioSessionRouteChangeReasonOldDeviceUnavailable,
                                                       AVAudioSessionRouteChangeReasonNewDeviceAvailable,
                                                       AVAudioSessionRouteChangeReasonCategoryChange};
    int n = 0;
    for (auto reason : reasons) {
        routeChange(reason);
        ++n;
        ET_CHECK(gState.waitFor(event_rerouted, n), "Dart received rerouted #%d (reason %lu)", n,
                 static_cast<unsigned long>(reason));
        ET_CHECK(mixing(h, 400), "still mixing after reason %lu", static_cast<unsigned long>(reason));
    }

    post(AVAudioSessionMediaServicesWereResetNotification, nil);
    sleepMs(300);
    ET_CHECK(isInited() && mixing(h), "still mixing after a (simulated) media services reset");
    std::printf("  note: neither miniaudio nor the engine observes "
                "AVAudioSessionMediaServicesWereResetNotification; after a real reset the app has to "
                "reinitialize (a simulated one cannot show what CoreAudio does then)\n");
    dispose();
}

static void deinitDuringInterruptionCase() {
    std::printf("[case] deinit during an interruption, then init\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    interruptionBegan();
    ET_CHECK(gState.waitFor(event_interruption_began), "interrupted");
    dispose();
    ET_CHECK(!isInited(), "deinit");
    // A late Ended for the engine that is gone.
    interruptionEnded(true);
    sleepMs(300);
    ET_CHECK(getAudioDeviceState() == audioDeviceUninitialized, "a late interruption_ended is harmless");
    ET_CHECK(initLikeDart() == noError, "init again");
    const unsigned int h2 = playSine();
    ET_CHECK(h2 != 0 && waitDeviceState(audioDeviceStarted) && mixing(h2),
             "the new engine plays (no interruption latched from before)");
    dispose();
}

static int runAll() {
    endedCase(true);
    endedCase(false);
    idleEndedCase();
    routeCase();
    deinitDuringInterruptionCase();
    return finish("audio session interruption");
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    std::thread worker([] { std::exit(runAll()); });
    worker.detach();
    CFRunLoopRun();
    return 0;
}

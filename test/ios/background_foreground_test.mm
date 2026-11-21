// App background/foreground transitions on iOS with audio playing and paused,
// end to end through the engine (soloud_miniaudio.cpp's lifecycle observers
// and synthesizeIosInterruptionEnded(), Player's interruption handling).
//
// A suspended app gets AVAudioSessionInterruptionNotification (Began) when it
// thaws and, from iOS 16, often no Ended: the engine synthesizes the Ended
// when the app returns to the foreground, or playback would stay silent
// (#573). Runs in the iOS simulator; the UIApplication / UIScene lifecycle
// notifications and the interruption are posted on NSNotificationCenter in
// the orders iOS delivers them:
//   * background -> Began (delivered while in the background) -> foreground:
//     interruption_ended reaches Dart, the device restarts, the voice mixes;
//   * background -> foreground -> late Began right after DidBecomeActive
//     (iOS 16+): ended is synthesized after the settle window;
//   * Began with the wasSuspended reason while active: ended at once;
//   * a genuine interruption (a call: only WillResignActive, no background)
//     is NOT ended by DidBecomeActive: the engine waits for the real Ended;
//   * a paused voice stays paused across background/foreground, keeps its
//     position, and resumes from there;
//   * no voice-ended callback is lost or duplicated: a short sound that ends
//     after the return reports exactly once;
//   * the engine time does not run while the device is stopped and runs again
//     after the return (no jump).
// The main thread runs the run loop (the observers use the main queue); the
// checks run on a worker thread.
//
// Build & run with test/ios/background_foreground_test.sh.

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include "../support/engine_ffi.h"

#include <cmath>

extern "C" void stopScheduled(unsigned int handle, double atTime);

using namespace enginetest;

static void postApp(const char *name) {
    @autoreleasepool {
        NSString *n = [NSString stringWithUTF8String:name];
        dispatch_sync(dispatch_get_main_queue(), ^{
            [[NSNotificationCenter defaultCenter] postNotificationName:n object:nil];
        });
    }
}

static void enterBackground() {
    postApp("UIApplicationWillResignActiveNotification");
    postApp("UISceneWillDeactivateNotification");
    postApp("UIApplicationDidEnterBackgroundNotification");
    postApp("UISceneDidEnterBackgroundNotification");
}

static void enterForeground() {
    postApp("UISceneWillEnterForegroundNotification");
    postApp("UIApplicationWillEnterForegroundNotification");
    postApp("UISceneDidActivateNotification");
    postApp("UIApplicationDidBecomeActiveNotification");
}

static void began(bool wasSuspendedReason) {
    @autoreleasepool {
        NSMutableDictionary *info =
            [@{AVAudioSessionInterruptionTypeKey : @(AVAudioSessionInterruptionTypeBegan)} mutableCopy];
        if (wasSuspendedReason) {
            if (@available(iOS 14.5, *)) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                info[AVAudioSessionInterruptionReasonKey] = @(AVAudioSessionInterruptionReasonAppWasSuspended);
#pragma clang diagnostic pop
            }
        }
        [[NSNotificationCenter defaultCenter] postNotificationName:AVAudioSessionInterruptionNotification
                                                            object:[AVAudioSession sharedInstance]
                                                          userInfo:info];
    }
}

static void ended() {
    @autoreleasepool {
        [[NSNotificationCenter defaultCenter]
            postNotificationName:AVAudioSessionInterruptionNotification
                          object:[AVAudioSession sharedInstance]
                        userInfo:@{
                            AVAudioSessionInterruptionTypeKey : @(AVAudioSessionInterruptionTypeEnded),
                            AVAudioSessionInterruptionOptionKey : @(AVAudioSessionInterruptionOptionShouldResume)
                        }];
    }
}

static unsigned int startPlaying() {
    gState.clear();
    if (initLikeDart() != noError) return 0;
    const unsigned int h = playSine();
    if (h == 0 || !waitDeviceState(audioDeviceStarted)) return 0;
    return h;
}

static void suspendedInBackgroundCase() {
    std::printf("[case] background, Began while in the background, foreground (no Ended)\n");
    const unsigned int h = startPlaying();
    ET_CHECK(h != 0 && mixing(h), "playing");
    enterBackground();
    began(false);
    ET_CHECK(gState.waitFor(event_interruption_began) && waitDeviceState(audioDeviceStopped),
             "interruption began, device stopped (%s)", gState.describe().c_str());
    const double engineBefore = getEngineTime();
    sleepMs(500);
    const double stalled = getEngineTime() - engineBefore;
    ET_CHECK(stalled < 0.05, "the engine time does not run while stopped (%.3f s)", stalled);
    enterForeground();
    ET_CHECK(gState.waitFor(event_interruption_ended), "Dart received a synthesized interruption_ended (%s)",
             gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device restarted (state %d)",
             static_cast<int>(getAudioDeviceState()));
    const double t0 = getEngineTime();
    const double adv = streamAdvance(h, 700);
    const double engineAdv = getEngineTime() - t0;
    ET_CHECK(adv > 0.35 && adv < 1.0, "the voice mixes again at real time (%.3f s in 0.7 s)", adv);
    ET_CHECK(engineAdv > 0.35 && engineAdv < 1.0, "the engine time runs again, no jump (%.3f s in 0.7 s)",
             engineAdv);
    ET_CHECK(gState.count(event_interruption_ended) == 1, "exactly one interruption_ended (%d)",
             gState.count(event_interruption_ended));
    dispose();
}

static void lateBeganCase() {
    std::printf("[case] background, foreground, then a late Began right after DidBecomeActive\n");
    const unsigned int h = startPlaying();
    ET_CHECK(h != 0, "playing");
    enterBackground();
    sleepMs(100);
    enterForeground();
    began(false);
    ET_CHECK(gState.waitFor(event_interruption_began), "interruption began");
    ET_CHECK(gState.waitFor(event_interruption_ended, 1, 2000), "ended synthesized after the settle window (%s)",
             gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStarted) && mixing(h), "playing again");
    dispose();
}

static void wasSuspendedReasonCase() {
    std::printf("[case] Began with the wasSuspended reason while active\n");
    const unsigned int h = startPlaying();
    ET_CHECK(h != 0, "playing");
    began(true);
    ET_CHECK(gState.waitFor(event_interruption_ended), "ended synthesized (%s)", gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStarted) && mixing(h), "playing again");
    dispose();
}

static void genuineInterruptionCase() {
    std::printf("[case] a call: inactive only, Began, active again, then the real Ended\n");
    const unsigned int h = startPlaying();
    ET_CHECK(h != 0, "playing");
    postApp("UIApplicationWillResignActiveNotification");
    began(false);
    ET_CHECK(gState.waitFor(event_interruption_began) && waitDeviceState(audioDeviceStopped), "interrupted");
    postApp("UIApplicationDidBecomeActiveNotification");
    sleepMs(800);
    ET_CHECK(gState.count(event_interruption_ended) == 0, "not ended by becoming active (%s)",
             gState.describe().c_str());
    ET_CHECK(getAudioDeviceState() == audioDeviceStopped, "device still stopped");
    ended();
    ET_CHECK(gState.waitFor(event_interruption_ended), "ended by the real Ended");
    ET_CHECK(waitDeviceState(audioDeviceStarted) && mixing(h), "playing again");
    dispose();
}

static void pausedVoiceCase() {
    std::printf("[case] a paused voice across background/foreground\n");
    const unsigned int playing = startPlaying();
    ET_CHECK(playing != 0, "playing");
    const unsigned int paused = playSine(0.02f);
    sleepMs(300);
    setPause(paused, true);
    sleepMs(100);
    const double pos = getStreamTime(paused);
    enterBackground();
    began(false);
    gState.waitFor(event_interruption_began);
    sleepMs(300);
    enterForeground();
    ET_CHECK(gState.waitFor(event_interruption_ended) && waitDeviceState(audioDeviceStarted), "back and running");
    sleepMs(400);
    ET_CHECK(getIsValidVoiceHandle(paused) && std::fabs(getStreamTime(paused) - pos) < 1e-6,
             "the paused voice kept its position (%.3f -> %.3f)", pos, getStreamTime(paused));
    setPause(paused, false);
    ET_CHECK(mixing(paused), "and resumes from there");
    dispose();
}

static void voiceEndedCase() {
    std::printf("[case] voice-ended callbacks across background/foreground\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    unsigned int hash = 0;
    ET_CHECK(loadWaveform(0, false, 0.25f, 1.0f, &hash) == noError, "waveform");
    // A sound that stops on its own: a non-looping voice with an end point
    // is not available for waveforms, so schedule a stop through the
    // engine's own clock instead -- not a timer of ours.
    unsigned int keep = playSine(0.02f);
    unsigned int h = 0;
    ET_CHECK(play(hash, 0, 0.02f, 0, false, false, 0, 0, -1, -1, 1.0f, &h) == noError, "play");
    waitDeviceState(audioDeviceStarted);
    stopScheduled(h, getEngineTime() + 0.8);
    enterBackground();
    began(false);
    gState.waitFor(event_interruption_began);
    sleepMs(1200); // the engine clock is stopped: the scheduled stop has not happened
    ET_CHECK(gState.voiceEnded == 0, "no voice ended while the device was stopped (%d)", gState.voiceEnded.load());
    ET_CHECK(getIsValidVoiceHandle(h), "the voice is still there");
    enterForeground();
    gState.waitFor(event_interruption_ended);
    for (int i = 0; i < 300 && gState.voiceEnded == 0; ++i) sleepMs(10);
    sleepMs(300);
    ET_CHECK(gState.voiceEnded == 1, "exactly one voice-ended callback after the return (%d)",
             gState.voiceEnded.load());
    ET_CHECK(!getIsValidVoiceHandle(h) && getIsValidVoiceHandle(keep), "the right voice ended");
    dispose();
}

static int runAll() {
    suspendedInBackgroundCase();
    lateBeganCase();
    wasSuspendedReasonCase();
    genuineInterruptionCase();
    pausedVoiceCase();
    voiceEndedCase();
    return finish("background/foreground");
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    std::thread worker([] { std::exit(runAll()); });
    worker.detach();
    CFRunLoopRun();
    return 0;
}

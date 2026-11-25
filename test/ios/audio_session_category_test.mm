// The engine leaves the app's AVAudioSession alone on iOS: the context is
// created with sessionCategory = none and noAudioSessionActivate /
// noAudioSessionDeactivate (init_platform_context() in soloud_miniaudio.cpp),
// so the category, mode and options the app chose must survive init, play,
// interruptions and deinit, and the engine must never deactivate the session.
//
// Runs in the iOS simulator with the whole engine. The AVAudioSession
// setters (setCategory:..., setMode:..., setActive:...) are swizzled to
// record every call the engine makes (the test's own calls are not counted),
// and the category / mode / options are read back after each step, for two
// app configurations (Playback + mixWithOthers + spokenAudio mode, and
// Ambient).
//
// Activation is allowed in one place: the engine calls setActive:YES before
// restarting the device from stopped (the MA_APPLE_MOBILE start path in
// soloud_miniaudio.cpp, for remote-command routing after interruptions). The
// test prints how often that happened; it never deactivates.
//
// Build & run with test/ios/audio_session_category_test.sh.

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

#include "../support/engine_ffi.h"

using namespace enginetest;

static std::atomic<bool> gRecording{false};
static std::atomic<int> gSetCategoryCalls{0}, gSetModeCalls{0}, gActivateCalls{0}, gDeactivateCalls{0};

static void resetCounts() {
    gSetCategoryCalls = gSetModeCalls = gActivateCalls = gDeactivateCalls = 0;
}

// Swizzling: each replacement records the call (while recording) and calls
// the original implementation.
template <typename Fn> struct Orig { static Fn fn; };
template <typename Fn> Fn Orig<Fn>::fn = nullptr;

typedef BOOL (*SetCatFn)(id, SEL, NSString *, NSError **);
typedef BOOL (*SetCatOptsFn)(id, SEL, NSString *, AVAudioSessionCategoryOptions, NSError **);
typedef BOOL (*SetCatModeOptsFn)(id, SEL, NSString *, NSString *, AVAudioSessionCategoryOptions, NSError **);
typedef BOOL (*SetModeFn)(id, SEL, NSString *, NSError **);
typedef BOOL (*SetActiveFn)(id, SEL, BOOL, NSError **);
typedef BOOL (*SetActiveOptsFn)(id, SEL, BOOL, AVAudioSessionSetActiveOptions, NSError **);

static SetCatFn oSetCat;
static SetCatOptsFn oSetCatOpts;
static SetCatModeOptsFn oSetCatModeOpts;
static SetModeFn oSetMode;
static SetActiveFn oSetActive;
static SetActiveOptsFn oSetActiveOpts;

static BOOL rSetCat(id s, SEL c, NSString *a, NSError **e) {
    if (gRecording) gSetCategoryCalls++;
    return oSetCat(s, c, a, e);
}
static BOOL rSetCatOpts(id s, SEL c, NSString *a, AVAudioSessionCategoryOptions o, NSError **e) {
    if (gRecording) gSetCategoryCalls++;
    return oSetCatOpts(s, c, a, o, e);
}
static BOOL rSetCatModeOpts(id s, SEL c, NSString *a, NSString *m, AVAudioSessionCategoryOptions o, NSError **e) {
    if (gRecording) gSetCategoryCalls++;
    return oSetCatModeOpts(s, c, a, m, o, e);
}
static BOOL rSetMode(id s, SEL c, NSString *m, NSError **e) {
    if (gRecording) gSetModeCalls++;
    return oSetMode(s, c, m, e);
}
static BOOL rSetActive(id s, SEL c, BOOL active, NSError **e) {
    if (gRecording) (active ? gActivateCalls : gDeactivateCalls)++;
    return oSetActive(s, c, active, e);
}
static BOOL rSetActiveOpts(id s, SEL c, BOOL active, AVAudioSessionSetActiveOptions o, NSError **e) {
    if (gRecording) (active ? gActivateCalls : gDeactivateCalls)++;
    return oSetActiveOpts(s, c, active, o, e);
}

template <typename Fn> static void swizzle(SEL sel, Fn replacement, Fn *original) {
    Method m = class_getInstanceMethod([AVAudioSession class], sel);
    if (!m) {
        std::printf("  note: no -[AVAudioSession %s]\n", sel_getName(sel));
        return;
    }
    *original = reinterpret_cast<Fn>(method_setImplementation(m, reinterpret_cast<IMP>(replacement)));
}

struct SessionState {
    std::string category, mode;
    unsigned long options = 0;
};

static SessionState current() {
    @autoreleasepool {
        AVAudioSession *s = [AVAudioSession sharedInstance];
        return {[s.category UTF8String] ?: "", [s.mode UTF8String] ?: "", static_cast<unsigned long>(s.categoryOptions)};
    }
}

static void expectUnchanged(const SessionState &want, const char *step) {
    const SessionState now = current();
    ET_CHECK(now.category == want.category && now.mode == want.mode && now.options == want.options,
             "%s: category %s mode %s options 0x%lx (app set %s / %s / 0x%lx)", step, now.category.c_str(),
             now.mode.c_str(), now.options, want.category.c_str(), want.mode.c_str(), want.options);
    ET_CHECK(gSetCategoryCalls == 0 && gSetModeCalls == 0, "%s: engine called setCategory %d / setMode %d times",
             step, gSetCategoryCalls.load(), gSetModeCalls.load());
    ET_CHECK(gDeactivateCalls == 0, "%s: engine deactivated the session %d times", step, gDeactivateCalls.load());
}

static void runConfig(NSString *category, NSString *mode, AVAudioSessionCategoryOptions options) {
    std::printf("[case] app session: %s, %s, options 0x%lx\n", [category UTF8String], [mode UTF8String],
                static_cast<unsigned long>(options));
    @autoreleasepool {
        NSError *error = nil;
        BOOL ok = [[AVAudioSession sharedInstance] setCategory:category mode:mode options:options error:&error];
        ET_CHECK(ok, "the app sets its category (%s)", error ? [[error description] UTF8String] : "ok");
    }
    const SessionState want = current();
    resetCounts();
    gRecording = true;

    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    expectUnchanged(want, "after init");
    ET_CHECK(gActivateCalls == 0, "init does not activate the session (%d)", gActivateCalls.load());

    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted) && mixing(h), "play");
    expectUnchanged(want, "after play");
    ET_CHECK(gActivateCalls == 0, "the first play after init does not activate the session (%d)",
             gActivateCalls.load());

    // An idle stop and the next play restart the device from stopped.
    setAudioDeviceIdleTimeout(100);
    stop(h);
    ET_CHECK(waitDeviceState(audioDeviceStopped), "idle stop");
    const unsigned int h1 = playSine();
    ET_CHECK(h1 != 0 && waitDeviceState(audioDeviceStarted), "play restarts the device");
    std::printf("  note: setActive:YES calls so far (idle restart): %d\n", gActivateCalls.load());
    expectUnchanged(want, "after an idle stop and restart");
    setAudioDeviceIdleTimeout(-1);

    // An interruption and its end: re-activating then is what the comment
    // in the source describes, so it is not counted against L2.
    debugTriggerAudioInterruption(1);
    waitDeviceState(audioDeviceStopped);
    debugTriggerAudioInterruption(0);
    ET_CHECK(waitDeviceState(audioDeviceStarted), "restarted after an interruption");
    std::printf("  note: setActive:YES calls so far (with the interruption): %d\n", gActivateCalls.load());
    expectUnchanged(want, "after an interruption");

    stop(h1);
    ET_CHECK(stopAudioDevice(1) == noError, "stopAudioDevice");
    expectUnchanged(want, "after stopAudioDevice");

    dispose();
    expectUnchanged(want, "after deinit");
    gRecording = false;
}

static int runAll() {
    swizzle(@selector(setCategory:error:), rSetCat, &oSetCat);
    swizzle(@selector(setCategory:withOptions:error:), rSetCatOpts, &oSetCatOpts);
    swizzle(@selector(setCategory:mode:options:error:), rSetCatModeOpts, &oSetCatModeOpts);
    swizzle(@selector(setMode:error:), rSetMode, &oSetMode);
    swizzle(@selector(setActive:error:), rSetActive, &oSetActive);
    swizzle(@selector(setActive:withOptions:error:), rSetActiveOpts, &oSetActiveOpts);

    runConfig(AVAudioSessionCategoryPlayback, AVAudioSessionModeSpokenAudio,
              AVAudioSessionCategoryOptionMixWithOthers);
    runConfig(AVAudioSessionCategoryAmbient, AVAudioSessionModeDefault, 0);
    return finish("audio session category");
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    std::thread worker([] { std::exit(runAll()); });
    worker.detach();
    CFRunLoopRun();
    return 0;
}

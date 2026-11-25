// AAudio device disconnection (headphones unplugged, a Bluetooth device gone)
// on Android, end to end through the engine.
//
// AAudio reports a disconnect through the stream's error callback; the
// engine installs gateAAudioStreamError() (soloud_miniaudio.cpp) in front of
// miniaudio's, which queues a reroute job that closes the stream and opens a
// replacement on the context's job thread. The test reports
// AAUDIO_ERROR_DISCONNECTED exactly the way AAudio does, through that gate,
// for the live stream (and later for stale ones), and checks:
//   * playing: the stream is replaced, the device keeps running, the voice
//     keeps mixing, and at real-time rate (two streams feeding the engine at
//     once would make the stream time run faster than the wall clock);
//   * Dart receives "rerouted";
//   * a disconnect of a stopped device leaves it stopped (no surprise
//     restart), and play afterwards starts and mixes;
//   * exactly one stream is closed and one opened per disconnect;
//   * an error reported for a stream that was already closed is ignored;
//   * a burst of disconnects ends with one working stream;
//   * a disconnect immediately followed by deinit neither crashes nor hangs,
//     and the engine initializes again;
//   * the same with low latency enabled.
// test/android/aaudio_reroute_race_test.cpp and aaudio_close_race_test.cpp
// stress the races underneath; this one checks the observable outcome.
//
// Build & run with test/android/aaudio_disconnect_test.sh.

#include "../support/engine_ffi.h"

#include "soloud.h"
#include "backend/miniaudio/miniaudio.h"

using namespace enginetest;

struct ma_AAudioStream_t;
typedef ma_AAudioStream_t *ma_AAudioStream;

namespace SoLoud {
extern ma_context context;
extern ma_device gDevice;
void gateAAudioStreamError(ma_AAudioStream *pStream, void *pUserData, int32_t error);
}

static constexpr int32_t kAAudioErrorDisconnected = -899; // AAUDIO_ERROR_DISCONNECTED

static ma_AAudioStream *liveStream() {
    return static_cast<ma_AAudioStream *>(SoLoud::gDevice.aaudio.pStreamPlayback);
}

static void reportDisconnect(ma_AAudioStream *stream) {
    SoLoud::gateAAudioStreamError(stream, &SoLoud::gDevice, kAAudioErrorDisconnected);
}

// Stream opens and closes on the engine's context, counted by wrapping the
// context's AAudio entry points (on top of the engine's own interposition)
// after each init. A replacement stream can get the old one's address, so
// pointers alone cannot tell a reopened stream from the same one.
typedef int32_t (*OpenStreamFn)(void *builder, ma_AAudioStream **stream);
typedef int32_t (*CloseStreamFn)(ma_AAudioStream *stream);
static OpenStreamFn gNextOpen = nullptr;
static CloseStreamFn gNextClose = nullptr;
static std::atomic<int> gOpens{0};
static std::atomic<int> gCloses{0};

static int32_t countingOpen(void *builder, ma_AAudioStream **stream) {
    const int32_t r = gNextOpen(builder, stream);
    if (r == 0) gOpens++;
    return r;
}
static int32_t countingClose(ma_AAudioStream *stream) {
    gCloses++;
    return gNextClose(stream);
}
static void countStreams() {
    gOpens = 0;
    gCloses = 0;
    gNextOpen = reinterpret_cast<OpenStreamFn>(SoLoud::context.aaudio.AAudioStreamBuilder_openStream);
    gNextClose = reinterpret_cast<CloseStreamFn>(SoLoud::context.aaudio.AAudioStream_close);
    SoLoud::context.aaudio.AAudioStreamBuilder_openStream = reinterpret_cast<ma_proc>(countingOpen);
    SoLoud::context.aaudio.AAudioStream_close = reinterpret_cast<ma_proc>(countingClose);
}

/// Waits for a stream to have been closed and another opened.
static bool waitStreamReplaced(int closes, int opens, int ms = 5000) {
    for (int waited = 0; waited < ms; waited += 10) {
        if (gCloses.load() > closes && gOpens.load() > opens && liveStream() != nullptr) return true;
        sleepMs(10);
    }
    return false;
}

static void playingCase(bool lowLatency) {
    std::printf("[case] disconnect while playing (lowLatency %d)\n", lowLatency ? 1 : 0);
    gState.clear();
    ET_CHECK(initLikeDart(lowLatency) == noError, "init");
    ET_CHECK(SoLoud::context.backend == ma_backend_aaudio, "AAudio backend");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    ET_CHECK(mixing(h), "mixing before the disconnect");
    ma_AAudioStream *before = liveStream();
    ET_CHECK(before != nullptr, "an AAudio stream is open");
    countStreams();

    reportDisconnect(before);
    ET_CHECK(waitStreamReplaced(0, 0), "the stream was closed and a new one opened (%d closed, %d opened)",
             gCloses.load(), gOpens.load());
    ET_CHECK(gState.waitFor(event_rerouted), "Dart received 'rerouted' (%s)", gState.describe().c_str());
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device started after the reroute (state %d)",
             static_cast<int>(getAudioDeviceState()));
    ET_CHECK(getIsValidVoiceHandle(h), "the voice survived the reroute");
    const double adv = streamAdvance(h, 1000);
    ET_CHECK(adv > 0.6, "the voice mixes after the reroute (%.3f s in 1 s)", adv);
    ET_CHECK(adv < 1.3, "at real-time rate, one stream feeding the engine (%.3f s in 1 s)", adv);

    ET_CHECK(gOpens.load() == 1 && gCloses.load() == 1, "exactly one stream closed and one opened (%d / %d)",
             gCloses.load(), gOpens.load());

    // A late error for the stream that is gone (only when the replacement
    // did not reuse its address, which would make it the live stream).
    ma_AAudioStream *current = liveStream();
    if (current != before) {
        reportDisconnect(before);
        sleepMs(500);
        ET_CHECK(gOpens.load() == 1 && gCloses.load() == 1, "an error from the closed stream is ignored");
        ET_CHECK(gState.count(event_rerouted) == 1, "no second 'rerouted' for it (%d)",
                 gState.count(event_rerouted));
    } else {
        std::printf("  note: the new stream reuses the old address; stale-error check skipped\n");
    }
    ET_CHECK(mixing(h), "still mixing");

    // A late error for the last stream after deinit closed it: AAudio can
    // still deliver one, and there is no device left to reroute.
    ma_AAudioStream *last = liveStream();
    dispose();
    ET_CHECK(!isInited(), "deinit");
    reportDisconnect(last);
    sleepMs(300);
    ET_CHECK(getAudioDeviceState() == audioDeviceUninitialized && gOpens.load() == 1,
             "an error for the stream closed by deinit is ignored (state %d, %d opens)",
             static_cast<int>(getAudioDeviceState()), gOpens.load());
}

static void stoppedCase() {
    std::printf("[case] disconnect while the device is stopped\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    stop(h);
    ET_CHECK(stopAudioDevice(1) == noError && waitDeviceState(audioDeviceStopped), "device stopped");
    ma_AAudioStream *before = liveStream();
    if (before == nullptr) {
        std::printf("  note: no stream while stopped; nothing to disconnect\n");
    } else {
        const int startedBefore = gState.count(event_started);
        reportDisconnect(before);
        sleepMs(1500);
        ET_CHECK(getAudioDeviceState() == audioDeviceStopped, "the device stays stopped (state %d)",
                 static_cast<int>(getAudioDeviceState()));
        ET_CHECK(gState.count(event_started) == startedBefore, "no 'started' from the reroute (%s)",
                 gState.describe().c_str());
    }
    h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "play starts the device again");
    const double adv = streamAdvance(h, 1000);
    ET_CHECK(adv > 0.6 && adv < 1.3, "and mixes at real time (%.3f s in 1 s)", adv);
    dispose();
}

static void burstCase() {
    std::printf("[case] five disconnects in a row\n");
    gState.clear();
    ET_CHECK(initLikeDart() == noError, "init");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && waitDeviceState(audioDeviceStarted), "playing");
    for (int i = 0; i < 5; ++i) {
        ma_AAudioStream *s = liveStream();
        if (s) reportDisconnect(s);
        sleepMs(i % 2 ? 5 : 150);
    }
    sleepMs(2000);
    ET_CHECK(liveStream() != nullptr, "a stream is open at the end");
    ET_CHECK(waitDeviceState(audioDeviceStarted), "device started at the end (state %d)",
             static_cast<int>(getAudioDeviceState()));
    const double adv = streamAdvance(h, 1000);
    ET_CHECK(adv > 0.6 && adv < 1.3, "one stream mixing at real time (%.3f s in 1 s)", adv);
    ET_CHECK(gState.count(event_rerouted) >= 1, "Dart received 'rerouted' (%s)", gState.describe().c_str());
    dispose();
}

static void disconnectThenDeinitCase() {
    std::printf("[case] disconnect immediately followed by deinit, then init\n");
    int ok = 0;
    for (int i = 0; i < 10; ++i) {
        if (initLikeDart() != noError) continue;
        const unsigned int h = playSine();
        waitDeviceState(audioDeviceStarted);
        ma_AAudioStream *s = liveStream();
        if (s) reportDisconnect(s);
        if (i % 3 == 1) sleepMs(i);
        const auto t0 = std::chrono::steady_clock::now();
        dispose();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ok += h != 0 && !isInited() && ms < 3000;
        if (ms >= 3000) std::printf("  deinit took %.0f ms\n", ms);
    }
    ET_CHECK(ok == 10, "%d/10 disconnect+deinit cycles finished promptly", ok);
    ET_CHECK(initLikeDart() == noError, "init again afterwards");
    const unsigned int h = playSine();
    ET_CHECK(h != 0 && mixing(h, 700), "and it mixes");
    dispose();
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    playingCase(false);
    playingCase(true);
    stoppedCase();
    burstCase();
    disconnectThenDeinitCase();
    return finish("AAudio disconnect");
}

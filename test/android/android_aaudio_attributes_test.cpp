// setAndroidAAudioAttributes() on a real AAudio stream: which usage /
// contentType / capture policy the engine's output stream ends up with
// (miniaudio_setAndroidAAudioAttributes and the device config in
// soloud_miniaudio.cpp). Read back with AAudioStream_getUsage() & co. from
// the stream the engine opened, and, for "unmanaged", by recording the
// builder setters miniaudio calls when the device is reopened.
//
// Cases:
//   * 1 (media/music, the default), 2 (voice communication/speech) and
//     invalid values (3, 99, UINT_MAX -> media/music), set before init;
//   * 0 (unmanaged): the setters are not called at all, so the app's own
//     AudioAttributes stay in charge (AAudio's defaults apply);
//   * the screen-recorder capture policy is "all" in every mode;
//   * a call after init does not touch the open stream; changeDevice()
//     (what Dart's SoLoud.setAndroidAAudioAttributes does) applies it, voices
//     keep playing;
//   * a reroute after a disconnect keeps the attributes;
//   * lowLatency: true leaves the attributes alone (documented).
//
// Build & run with test/android/android_aaudio_attributes_test.sh.

#include "../support/engine_ffi.h"

#include "soloud.h"
#include "backend/miniaudio/miniaudio.h"

#include <climits>
#include <dlfcn.h>

using namespace enginetest;

struct ma_AAudioStream_t;
typedef ma_AAudioStream_t *ma_AAudioStream;

namespace SoLoud {
extern ma_context context;
extern ma_device gDevice;
void gateAAudioStreamError(ma_AAudioStream *pStream, void *pUserData, int32_t error);
}

// AAudio constants (aaudio/AAudio.h).
constexpr int32_t kUsageMedia = 1, kUsageVoiceCommunication = 2;
constexpr int32_t kContentSpeech = 1, kContentMusic = 2;
constexpr int32_t kCaptureAll = 1;

typedef int32_t (*GetFn)(ma_AAudioStream *);
static GetFn gGetUsage, gGetContentType, gGetCapturePolicy;

typedef void (*SetIntFn)(void *builder, int32_t value);
static SetIntFn gNextSetUsage, gNextSetContentType, gNextSetCapture;
static std::atomic<int> gSetUsageCalls{0}, gSetContentCalls{0}, gSetCaptureCalls{0};
static void recSetUsage(void *b, int32_t v) {
    gSetUsageCalls++;
    gNextSetUsage(b, v);
}
static void recSetContent(void *b, int32_t v) {
    gSetContentCalls++;
    gNextSetContentType(b, v);
}
static void recSetCapture(void *b, int32_t v) {
    gSetCaptureCalls++;
    gNextSetCapture(b, v);
}
/// Records the builder setters on the engine's context (after init).
static void recordSetters() {
    gSetUsageCalls = gSetContentCalls = gSetCaptureCalls = 0;
    auto &a = SoLoud::context.aaudio;
    gNextSetUsage = reinterpret_cast<SetIntFn>(a.AAudioStreamBuilder_setUsage);
    gNextSetContentType = reinterpret_cast<SetIntFn>(a.AAudioStreamBuilder_setContentType);
    gNextSetCapture = reinterpret_cast<SetIntFn>(a.AAudioStreamBuilder_setAllowedCapturePolicy);
    if (gNextSetUsage) a.AAudioStreamBuilder_setUsage = reinterpret_cast<ma_proc>(recSetUsage);
    if (gNextSetContentType) a.AAudioStreamBuilder_setContentType = reinterpret_cast<ma_proc>(recSetContent);
    if (gNextSetCapture) a.AAudioStreamBuilder_setAllowedCapturePolicy = reinterpret_cast<ma_proc>(recSetCapture);
}

struct Attrs {
    int32_t usage = -1, content = -1, capture = -1;
};

static Attrs streamAttrs() {
    Attrs a;
    auto *s = static_cast<ma_AAudioStream *>(SoLoud::gDevice.aaudio.pStreamPlayback);
    if (!s) return a;
    a.usage = gGetUsage ? gGetUsage(s) : -1;
    a.content = gGetContentType ? gGetContentType(s) : -1;
    a.capture = gGetCapturePolicy ? gGetCapturePolicy(s) : -1;
    return a;
}

static unsigned int initAndPlay(unsigned int attributes, bool lowLatency = false) {
    setAndroidAAudioAttributes(attributes);
    if (initLikeDart(lowLatency) != noError) return 0;
    const unsigned int h = playSine();
    waitDeviceState(audioDeviceStarted);
    return h;
}

static void expectAttrs(const char *what, int32_t usage, int32_t content) {
    const Attrs a = streamAttrs();
    ET_CHECK(a.usage == usage && a.content == content, "%s: usage %d contentType %d (expected %d / %d)", what,
             a.usage, a.content, usage, content);
    if (gGetCapturePolicy)
        ET_CHECK(a.capture == kCaptureAll, "%s: capture policy %d (expected ALL)", what, a.capture);
}

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    void *lib = dlopen("libaaudio.so", RTLD_NOW);
    if (lib) {
        gGetUsage = reinterpret_cast<GetFn>(dlsym(lib, "AAudioStream_getUsage"));
        gGetContentType = reinterpret_cast<GetFn>(dlsym(lib, "AAudioStream_getContentType"));
        gGetCapturePolicy = reinterpret_cast<GetFn>(dlsym(lib, "AAudioStream_getAllowedCapturePolicy"));
    }
    if (!gGetUsage || !gGetContentType) {
        std::printf("SKIPPED: AAudioStream_getUsage needs API 28+\n");
        return 0;
    }

    struct { unsigned int value; int32_t usage, content; const char *name; } before[] = {
        {1, kUsageMedia, kContentMusic, "1 media/music"},
        {2, kUsageVoiceCommunication, kContentSpeech, "2 voice communication"},
        {3, kUsageMedia, kContentMusic, "3 (invalid)"},
        {99, kUsageMedia, kContentMusic, "99 (invalid)"},
        {UINT_MAX, kUsageMedia, kContentMusic, "UINT_MAX (invalid)"},
    };
    for (const auto &c : before) {
        std::printf("[case] %s set before init\n", c.name);
        const unsigned int h = initAndPlay(c.value);
        ET_CHECK(h != 0 && SoLoud::context.backend == ma_backend_aaudio, "init and play on AAudio");
        expectAttrs(c.name, c.usage, c.content);
        ET_CHECK(mixing(h), "mixing");
        dispose();
    }

    std::printf("[case] 0 unmanaged: no usage/contentType setter on the builder\n");
    {
        const unsigned int h = initAndPlay(2);
        expectAttrs("voice before switching to unmanaged", kUsageVoiceCommunication, kContentSpeech);
        recordSetters();
        setAndroidAAudioAttributes(0);
        ET_CHECK(changeDevice(-1) == noError, "changeDevice applies it");
        ET_CHECK(gSetUsageCalls == 0 && gSetContentCalls == 0, "setUsage/setContentType not called (%d / %d)",
                 gSetUsageCalls.load(), gSetContentCalls.load());
        ET_CHECK(gSetCaptureCalls >= 1 || !gNextSetCapture, "the capture policy is still set (%d)",
                 gSetCaptureCalls.load());
        expectAttrs("unmanaged (AAudio defaults)", kUsageMedia, kContentMusic);
        ET_CHECK(getIsValidVoiceHandle(h) && mixing(h), "the voice keeps playing");
        dispose();
    }

    std::printf("[case] set after init: applied on the next device open only\n");
    {
        const unsigned int h = initAndPlay(1);
        expectAttrs("media at init", kUsageMedia, kContentMusic);
        setAndroidAAudioAttributes(2);
        sleepMs(200);
        expectAttrs("unchanged until the device is reopened", kUsageMedia, kContentMusic);
        recordSetters();
        ET_CHECK(changeDevice(-1) == noError, "changeDevice");
        ET_CHECK(gSetUsageCalls >= 1, "setUsage called on reopen (%d)", gSetUsageCalls.load());
        expectAttrs("voice after changeDevice", kUsageVoiceCommunication, kContentSpeech);
        ET_CHECK(getIsValidVoiceHandle(h) && mixing(h), "the voice keeps playing across the reopen");

        std::printf("[case] a reroute after a disconnect keeps the attributes\n");
        auto *s = static_cast<ma_AAudioStream *>(SoLoud::gDevice.aaudio.pStreamPlayback);
        gState.clear();
        SoLoud::gateAAudioStreamError(s, &SoLoud::gDevice, -899 /* AAUDIO_ERROR_DISCONNECTED */);
        ET_CHECK(gState.waitFor(event_rerouted), "rerouted (%s)", gState.describe().c_str());
        sleepMs(300);
        expectAttrs("voice after the reroute", kUsageVoiceCommunication, kContentSpeech);
        ET_CHECK(mixing(h), "mixing after the reroute");

        setAndroidAAudioAttributes(1);
        ET_CHECK(changeDevice(-1) == noError, "back to media");
        expectAttrs("media again", kUsageMedia, kContentMusic);
        dispose();
    }

    std::printf("[case] lowLatency: true leaves the attributes alone\n");
    {
        const unsigned int h = initAndPlay(2, true);
        ET_CHECK(h != 0, "init and play");
        const Attrs a = streamAttrs();
        ET_CHECK(a.usage == kUsageMedia && a.content == kContentMusic,
                 "low latency: AAudio defaults, not voice (usage %d contentType %d)", a.usage, a.content);
        dispose();
    }
    setAndroidAAudioAttributes(1);
    return finish("AAudio attributes");
}

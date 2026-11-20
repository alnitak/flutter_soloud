// The engine on WASAPI when the output device changes under it (Windows).
// Links the whole plugin (bindings.cpp, player.cpp, SoLoud, miniaudio) and
// drives it through the C entry points Dart uses.
//
//   1. Default device change while playing: with two or more active render
//      endpoints, the default endpoint is switched to another one (through
//      the IPolicyConfig interface the Sound control panel uses) and back.
//      miniaudio follows the default device, so playback keeps going: the
//      voice stays valid and the engine clock keeps advancing.
//   2. Exclusive device busy: another client opens the default endpoint in
//      exclusive mode. Initializing the engine then must not hang, and once
//      the other client lets go, startAudioDevice() / changeDevice() bring
//      playback back.
//   3. No render device at all: initEngine() must not crash or hang; it
//      either fails with an error or comes up on miniaudio's null backend,
//      and then the engine must really run.
//   4. Removal of the default device while playing (opt-in, needs an
//      administrator: wasapi_device_change_test.ps1 -AllowDeviceDisable
//      disables the endpoint with pnputil while this waits on signal files).
//
// Cases whose prerequisite is missing are reported as "not run"; when none
// can run the test prints SKIPPED.
//
//   wasapi_device_change_test.exe [--expect-no-device] [--removal <signal dir>] [--strict]

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "bindings.h"
#include "enums.h"

#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static int gChecks = 0, gFailures = 0, gKnownBugs = 0, gRan = 0;
static bool gStrict = false;

#define CHECK(cond, ...)                                                       \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL [%s:%d] %s: ", __FILE__, __LINE__, #cond);     \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

#define KNOWN_BUG(id, cond, ...)                                               \
    do {                                                                       \
        ++gChecks;                                                             \
        if (!(cond)) {                                                         \
            if (gStrict) ++gFailures; else ++gKnownBugs;                       \
            std::printf("  %s %s [%s:%d] %s: ", gStrict ? "FAIL" : "KNOWN BUG",\
                        id, __FILE__, __LINE__, #cond);                        \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

static void onVoiceEnded(unsigned int *h) { std::free(h); }
static void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h, uint64_t *c)
{
    std::free(e);
    std::free(n);
    std::free(h);
    std::free(c);
}
static std::atomic<int> gRerouted{0};
static void onStateChanged(enum PlayerStateEvents *s)
{
    if (s != nullptr && *s == event_rerouted) gRerouted++;
    std::free(s);
}

static long long nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static bool engineClockRuns(double minSeconds = 0.1, int budgetMs = 3000)
{
    const double start = getEngineTime();
    const long long end = nowMs() + budgetMs;
    while (nowMs() < end)
    {
        if (getEngineTime() - start >= minSeconds) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

// A call that never returns cannot be caught from inside: abort instead of
// hanging the suite.
static std::atomic<long long> gDeadline{0};
static std::atomic<const char *> gStep{""};
struct Guarded
{
    Guarded(const char *what, int ms)
    {
        gStep = what;
        gDeadline = nowMs() + ms;
    }
    ~Guarded() { gDeadline = 0; }
};
static void watchdog()
{
    for (;;)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const long long d = gDeadline.load();
        if (d != 0 && nowMs() > d)
        {
            std::printf("  FAIL: '%s' blocked past its budget (hang)\n", gStep.load());
            std::fflush(stdout);
            ExitProcess(3);
        }
    }
}

static bool bringUp(PlayerErrors *err = nullptr)
{
    prepareEngineInit(1);
    PlayerErrors e;
    {
        Guarded g("initEngine", 15000);
        e = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    }
    if (err) *err = e;
    if (e != noError) return false;
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, 1);
    return true;
}

static unsigned playTone()
{
    unsigned hash = 0, handle = 0;
    if (loadWaveform(0, false, 1.0f, 1.0f, &hash) != noError) return 0;
    play(hash, 0, 0.05f, 0, false, true, 0, 0, -1, -1, 1.0f, &handle);
    return handle;
}

static void tearDown()
{
    Guarded g("dispose", 15000);
    if (isInited()) dispose();
}

// --- WASAPI helpers ---------------------------------------------------------

struct Endpoint
{
    std::wstring id;
    std::wstring name;
};

static std::vector<Endpoint> renderEndpoints(std::wstring *defaultId)
{
    std::vector<Endpoint> out;
    IMMDeviceEnumerator *en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return out;
    IMMDeviceCollection *col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)))
    {
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; i++)
        {
            IMMDevice *dev = nullptr;
            if (FAILED(col->Item(i, &dev))) continue;
            Endpoint ep;
            LPWSTR id = nullptr;
            if (SUCCEEDED(dev->GetId(&id)))
            {
                ep.id = id;
                CoTaskMemFree(id);
            }
            IPropertyStore *props = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)))
            {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
                    ep.name = v.pwszVal;
                PropVariantClear(&v);
                props->Release();
            }
            out.push_back(ep);
            dev->Release();
        }
        col->Release();
    }
    if (defaultId != nullptr)
    {
        defaultId->clear();
        IMMDevice *def = nullptr;
        if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)))
        {
            LPWSTR id = nullptr;
            if (SUCCEEDED(def->GetId(&id)))
            {
                *defaultId = id;
                CoTaskMemFree(id);
            }
            def->Release();
        }
    }
    en->Release();
    return out;
}

// IPolicyConfig: undocumented but stable since Windows 7, used by the Sound
// control panel and every "switch default device" tool.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};
static const CLSID kPolicyConfigClient = {0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};
static const IID kIPolicyConfig = {0xf8679f50, 0x850a, 0x41cf, {0x9c, 0x72, 0x43, 0x0f, 0x29, 0x02, 0x90, 0xc8}};
// KSDATAFORMAT_SUBTYPE_PCM, spelled out so no ksmedia.h / ksguid.lib is needed.
static const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static bool setDefaultEndpoint(const std::wstring &id)
{
    IPolicyConfig *pc = nullptr;
    if (FAILED(CoCreateInstance(kPolicyConfigClient, nullptr, CLSCTX_ALL,
                                kIPolicyConfig, (void **)&pc)))
        return false;
    bool ok = true;
    for (ERole role : {eConsole, eMultimedia, eCommunications})
        ok = SUCCEEDED(pc->SetDefaultEndpoint(id.c_str(), role)) && ok;
    pc->Release();
    return ok;
}

// --- cases --------------------------------------------------------------------

static void testDefaultDeviceChange()
{
    std::printf("default device change while playing\n");
    std::wstring def;
    const auto eps = renderEndpoints(&def);
    if (eps.size() < 2 || def.empty())
    {
        std::printf("  not run: %zu active render endpoint(s), needs 2\n", eps.size());
        return;
    }
    std::wstring other;
    for (const auto &e : eps)
        if (e.id != def) other = e.id;
    if (!bringUp())
    {
        CHECK(false, "engine init with %zu endpoints", eps.size());
        return;
    }
    const unsigned h = playTone();
    CHECK(engineClockRuns(), "playing on the original default device");
    if (!setDefaultEndpoint(other))
    {
        std::printf("  not run: IPolicyConfig::SetDefaultEndpoint unavailable\n");
        tearDown();
        return;
    }
    gRan++;
    const int reroutesBefore = gRerouted.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(getIsValidVoiceHandle(h), "the voice survived the default device change");
    CHECK(engineClockRuns(), "playback continues on the new default device (state %d)", getAudioDeviceState());
    std::printf("  rerouted events: %d\n", gRerouted.load() - reroutesBefore);
    CHECK(gRerouted.load() > reroutesBefore, "a rerouted event was published");
    // And back, which must work just as well.
    setDefaultEndpoint(def);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(engineClockRuns(), "playback continues after switching back");
    // Rapid switches must not wedge the device.
    for (int i = 0; i < 6; i++)
    {
        setDefaultEndpoint(i % 2 ? def : other);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
    }
    setDefaultEndpoint(def);
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(engineClockRuns(), "playback continues after rapid default switches");
    tearDown();
}

static IAudioClient *openExclusive()
{
    IMMDeviceEnumerator *en = nullptr;
    IMMDevice *dev = nullptr;
    IAudioClient *client = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void **)&en)))
        return nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) &&
        SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&client)))
    {
        REFERENCE_TIME def = 0, minP = 0;
        client->GetDevicePeriod(&def, &minP);
        bool opened = false;
        for (DWORD rate : {48000u, 44100u, 96000u})
        {
            for (WORD bits : {16, 24, 32})
            {
                WAVEFORMATEXTENSIBLE f{};
                f.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                f.Format.nChannels = 2;
                f.Format.nSamplesPerSec = rate;
                f.Format.wBitsPerSample = bits;
                f.Format.nBlockAlign = (WORD)(2 * bits / 8);
                f.Format.nAvgBytesPerSec = rate * f.Format.nBlockAlign;
                f.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
                f.Samples.wValidBitsPerSample = bits;
                f.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
                f.SubFormat = kSubtypePcm;
                if (client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &f.Format, nullptr) != S_OK)
                    continue;
                if (SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, def, def, &f.Format, nullptr)))
                {
                    opened = true;
                    break;
                }
            }
            if (opened) break;
        }
        if (opened && SUCCEEDED(client->Start()))
        {
            dev->Release();
            en->Release();
            return client;
        }
        client->Release();
        client = nullptr;
    }
    if (dev) dev->Release();
    en->Release();
    return nullptr;
}

static void testExclusiveBusy()
{
    std::printf("default device held in exclusive mode by another client\n");
    std::wstring def;
    if (renderEndpoints(&def).empty())
    {
        std::printf("  not run: no render endpoint\n");
        return;
    }
    IAudioClient *holder = openExclusive();
    if (holder == nullptr)
    {
        std::printf("  not run: exclusive mode unavailable on the default endpoint\n");
        return;
    }
    gRan++;
    PlayerErrors err = noError;
    const bool up = bringUp(&err);
    std::printf("  initEngine() with the device busy -> %d, state %d\n", err, getAudioDeviceState());
    unsigned h = 0;
    if (up)
    {
        h = playTone();
        const bool runs = engineClockRuns(0.1, 1000);
        std::printf("  clock %s while the device is busy\n", runs ? "advancing (?)" : "stopped");
        PlayerErrors s;
        {
            Guarded g("startAudioDevice with the device busy", 15000);
            s = startAudioDevice();
        }
        std::printf("  startAudioDevice() -> %d\n", s);
        CHECK(s != noError || runs, "startAudioDevice() with the device busy must report it or really run");
    }
    holder->Stop();
    holder->Release();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (!up)
    {
        CHECK(bringUp(&err), "initEngine() once the device is free -> %d", err);
        h = playTone();
    }
    else
    {
        PlayerErrors s;
        {
            Guarded g("startAudioDevice once free", 15000);
            s = startAudioDevice();
        }
        if (!engineClockRuns(0.1, 1500))
        {
            Guarded g("changeDevice once free", 15000);
            s = changeDevice(-1);
            startAudioDevice();
        }
        std::printf("  recovery -> %d\n", s);
    }
    CHECK(engineClockRuns(), "playback works once the device is free (state %d)", getAudioDeviceState());
    CHECK(h == 0 || getIsValidVoiceHandle(h), "the voice survived");
    tearDown();
}

static void testNoDevice(bool expectNoDevice)
{
    std::printf("no render device\n");
    std::wstring def;
    const auto eps = renderEndpoints(&def);
    if (!eps.empty())
    {
        CHECK(!expectNoDevice, "--expect-no-device, but %zu endpoint(s) are active", eps.size());
        std::printf("  not run: %zu active render endpoint(s)\n", eps.size());
        return;
    }
    gRan++;
    PlayerErrors err = noError;
    const bool up = bringUp(&err);
    std::printf("  initEngine() -> %d, state %d\n", err, getAudioDeviceState());
    // miniaudio's default backend list ends with its null backend, so with
    // no endpoint the engine is expected to come up on it (silently), as
    // engine_lifecycle_test also assumes. Either way: no crash, no hang, and
    // an engine that says it is up must really run.
    if (up)
    {
        const unsigned h = playTone();
        PlayerErrors s;
        {
            Guarded g("startAudioDevice without a device", 15000);
            s = startAudioDevice();
        }
        const bool runs = engineClockRuns();
        std::printf("  play -> handle %u, startAudioDevice() -> %d, clock %s\n", h, s,
                    runs ? "advancing (null backend)" : "stopped");
        CHECK(h != 0 && s == noError && runs,
              "init reported success without a device, so the engine must run (start %d, clock %s)", s,
              runs ? "advancing" : "stopped");
    }
    else
    {
        CHECK(err != noError, "a failed init reports an error");
    }
    tearDown();
}

static bool waitFile(const std::string &path, int ms)
{
    const long long end = nowMs() + ms;
    while (nowMs() < end)
    {
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

static void testRemoval(const std::string &dir)
{
    std::printf("default device removed while playing\n");
    std::wstring def;
    if (renderEndpoints(&def).empty() || !bringUp())
    {
        CHECK(false, "no device to remove");
        return;
    }
    gRan++;
    const unsigned h = playTone();
    CHECK(engineClockRuns(), "playing before the removal");
    // Tell the script which endpoint to disable.
    if (FILE *f = std::fopen((dir + "\\endpoint.txt").c_str(), "w"))
    {
        std::fprintf(f, "%ls\n", def.c_str());
        std::fclose(f);
    }
    CHECK(waitFile(dir + "\\removed", 60000), "the script disabled the endpoint");
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    {
        Guarded g("calls after the removal", 15000);
        (void)getAudioDeviceState();
        (void)getEngineTime();
        unsigned h2 = playTone();
        (void)h2;
    }
    std::wstring now;
    const auto left = renderEndpoints(&now);
    std::printf("  %zu endpoint(s) left, state %d\n", left.size(), getAudioDeviceState());
    if (!left.empty())
        CHECK(engineClockRuns(), "playback moved to the next default device");
    if (FILE *f = std::fopen((dir + "\\checked").c_str(), "w")) std::fclose(f);
    CHECK(waitFile(dir + "\\restored", 60000), "the script re-enabled the endpoint");
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (!engineClockRuns(0.1, 1500))
    {
        Guarded g("recovery after the endpoint came back", 15000);
        startAudioDevice();
    }
    CHECK(engineClockRuns(), "playback after the endpoint came back (state %d)", getAudioDeviceState());
    CHECK(getIsValidVoiceHandle(h), "the voice survived the removal");
    tearDown();
}

int main(int argc, char **argv)
{
    bool expectNoDevice = false;
    std::string removalDir;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--strict") == 0) gStrict = true;
        else if (std::strcmp(argv[i], "--expect-no-device") == 0) expectNoDevice = true;
        else if (std::strcmp(argv[i], "--removal") == 0 && i + 1 < argc) removalDir = argv[++i];
    }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::thread(watchdog).detach();

    if (!removalDir.empty())
    {
        testRemoval(removalDir);
    }
    else
    {
        testNoDevice(expectNoDevice);
        if (!expectNoDevice)
        {
            testDefaultDeviceChange();
            testExclusiveBusy();
        }
    }

    if (gRan == 0 && gFailures == 0)
    {
        std::printf("SKIPPED: no case could run here (see the 'not run' lines)\n");
        return 0;
    }
    std::printf("%d case(s) run, %d checks, %d failed, %d known bugs\n", gRan, gChecks, gFailures, gKnownBugs);
    return gFailures == 0 ? 0 : 1;
}

// Standalone robustness test of every function src/bindings.cpp exports to
// Dart (the FFI surface declared in src/bindings.h).
//
// Each exported function is called with arguments Dart could pass by mistake
// or after a race:
//
//   - phase "pre":    before initEngine();
//   - phase "inited": with the engine running on miniaudio's null backend,
//                     with handle 0, made-up handles (0x0BAD0BAD, 0xFFFFFFFF),
//                     sound hash 0 / unknown, unknown bus ids, null pointers,
//                     zero and negative lengths, out-of-range enums and
//                     custom filter ids, NaN / Inf floats;
//   - phase "post":   after dispose().
//
// No call may crash or hang, and a function that returns PlayerErrors must
// return an error code (not noError) for an invalid input or a missing
// engine. Functions for which noError is the documented answer (global
// setters that need no engine, idempotent stops) are called with CALL(),
// and a comment says why.
//
// Isolation: the parent never starts the engine. For every (function, phase)
// it forks a child that sets up the phase, runs the function's cases and
// reports each step through a pipe. A crash (signal, sanitizer abort) or a
// hang (alarm) is attributed to the last step the child announced, so one
// crash does not hide the others.
//
// Known product bugs are listed in kKnownBugs below: they print
// "KNOWN BUG <ID> (...)" and fail the run only with --strict.
//
//   ./test/common/bindings_null_args_test.sh [--asan] [--strict] [filter]

#include "bindings.h"
#include "enums.h"

#include "../support/robustness_checks.h"
#include "../support/line_buffered_stdout.h"

#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <set>
#include <limits>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

enum Phase { kPre = 0, kInited = 1, kPost = 2 };
const char *kPhaseName[] = {"pre", "inited", "post"};

int gPhase = kPre;
int gPipe = -1;
const char *gCase = "";
char gCtx[64] = "";

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const unsigned int kBadHandles[] = {0u, 0x0BAD0BADu, 0xFFFFFFFFu};
const unsigned int kBadHashes[] = {0u, 0x0BAD0BADu, 0xFFFFFFFFu};
const unsigned int kBadBuses[] = {0x0BAD0BADu, 0xFFFFFFFFu};

void sendLine(const char *kind, const char *text) {
  char buf[512];
  int n = std::snprintf(buf, sizeof(buf), "%s %s: %s%s%s\n", kind, gCase, text,
                        gCtx[0] ? " " : "", gCtx);
  if (n > static_cast<int>(sizeof(buf)) - 1) {
    n = sizeof(buf) - 1;
    buf[n - 1] = '\n';
  }
  ssize_t w = write(gPipe, buf, static_cast<size_t>(n));
  (void)w;
}

void ctx(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(gCtx, sizeof(gCtx), fmt, ap);
  va_end(ap);
}

// Steps are numbered in the order a case runs them. A step that crashed is
// skipped when the parent re-runs the case, so the steps after it still run.
int gStepNo = 0;
std::vector<int> gSkip;
bool step(const char *text) {
  ++gStepNo;
  for (int k : gSkip)
    if (k == gStepNo) return false;
  char tag[16];
  std::snprintf(tag, sizeof(tag), "S%d", gStepNo);
  sendLine(tag, text);
  return true;
}
void failNoError(const char *text) { sendLine("F", text); }

// Call and require nothing but survival.
#define CALL(expr)                                                             \
  do {                                                                         \
    if (step(#expr)) (void)(expr);                                             \
  } while (0)
// Call a PlayerErrors function whose input is invalid (or whose engine is
// missing): it must not return noError.
#define ERR(expr)                                                              \
  do {                                                                         \
    if (step(#expr)) {                                                         \
      PlayerErrors r_ = (expr);                                                \
      if (r_ == noError) failNoError(#expr " returned noError");               \
    }                                                                          \
  } while (0)
// Same, for functions whose failure value is 0 (handles / ids / counts).
#define ZERO(expr)                                                             \
  do {                                                                         \
    if (step(#expr)) {                                                         \
      auto r_ = (expr);                                                        \
      if (r_ != 0) failNoError(#expr " returned non-zero");                    \
    }                                                                          \
  } while (0)

bool inited() { return gPhase == kInited; }

// ---- a real sound, for the cases that need a valid hash or handle ---------
std::vector<unsigned char> wavBytes() {
  const unsigned int rate = 44100, frames = 4410;
  std::vector<unsigned char> b(44 + frames * 2);
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); };
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); };
  std::memcpy(&b[0], "RIFF", 4);
  put32(4, 36 + frames * 2);
  std::memcpy(&b[8], "WAVEfmt ", 8);
  put32(16, 16);
  put16(20, 1);
  put16(22, 1);
  put32(24, rate);
  put32(28, rate * 2);
  put16(32, 2);
  put16(34, 16);
  std::memcpy(&b[36], "data", 4);
  put32(40, frames * 2);
  for (unsigned i = 0; i < frames; ++i)
    put16(44 + i * 2, static_cast<uint16_t>(
                          static_cast<int16_t>(8000 * std::sin(i * 0.06))));
  return b;
}
std::vector<unsigned char> gWav = wavBytes();

unsigned int validSound() {
  if (!inited()) return 0;
  static unsigned int counter = 0;
  char name[64];
  std::snprintf(name, sizeof(name), "valid_%u.wav", ++counter);
  unsigned int hash = 0;
  if (loadMem(name, gWav.data(), static_cast<int>(gWav.size()), 1, &hash) !=
      noError)
    return 0;
  return hash;
}
unsigned int validHandle() {
  const unsigned int hash = validSound();
  if (hash == 0) return 0;
  unsigned int handle = 0;
  if (play(hash, 0, 1.0f, 0.0f, true, false, 0, 0, -1, -1, 1.0f, &handle) !=
      noError)
    return 0;
  return handle;
}

// ---- cases -----------------------------------------------------------------
struct Case {
  const char *name;
  std::function<void()> run;
};
std::vector<Case> &cases() {
  static std::vector<Case> c;
  return c;
}
struct Register {
  Register(const char *n, std::function<void()> f) {
    cases().push_back({n, std::move(f)});
  }
};
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define CASE(name)                                                             \
  static void CAT(case_, __LINE__)();                                          \
  static Register CAT(reg_, __LINE__)(name, CAT(case_, __LINE__));             \
  static void CAT(case_, __LINE__)()

#define FOR_HANDLES for (unsigned int h : kBadHandles) if ((ctx("h=0x%x", h), true))
#define FOR_HASHES for (unsigned int s : kBadHashes) if ((ctx("hash=0x%x", s), true))
#define FOR_BUSES for (unsigned int b : kBadBuses) if ((ctx("bus=0x%x", b), true))

void noopVoiceEnded(unsigned int *) {}
void noopFileLoaded(enum PlayerErrors *, char *, unsigned int *, uint64_t *,
                    uint64_t *) {}
void noopStateChanged(enum PlayerStateEvents *) {}

// --- engine / callbacks -----------------------------------------------------

CASE("nativeFree") { CALL((nativeFree(nullptr), 0)); }
CASE("voiceEndedCallback") {
  unsigned int h = 0x0BAD0BAD;
  CALL((voiceEndedCallback(&h), 0));
  CALL((voiceEndedCallback(nullptr), 0));
}
CASE("voiceInactiveCallback") { CALL((voiceInactiveCallback(), 0)); }
CASE("setDartEventCallback") {
  CALL((setDartEventCallback(nullptr, nullptr, nullptr, -1), 0));
  CALL((setDartEventCallback(nullptr, nullptr, nullptr, 12345), 0));
}
CASE("clearDartCallbackRegistrations") {
  CALL((clearDartCallbackRegistrations(), 0));
  CALL(clearDartCallbackRegistrationsForEngine(-1));
  CALL(clearDartCallbackRegistrationsForEngine(987654));
}
CASE("retireDartCallbacksFinalizer") {
  CALL((retireDartCallbacksFinalizer(nullptr), 0));
  int dummy = 0;
  CALL((retireDartCallbacksFinalizer(&dummy), 0));
}

// --- mixer capture ----------------------------------------------------------
CASE("startMixerCapture") {
  ERR(startMixerCapture(-1, 44100, 2, 4096, 1024, 0));
  ERR(startMixerCapture(99, 44100, 2, 4096, 1024, 0));
  // sampleRate / channels <= 0 follow the engine (documented): fine.
  CALL(startMixerCapture(0, 0, 2, 4096, 1024, 0));
  CALL((stopMixerCapture(), 0));
  CALL(startMixerCapture(0, -44100, -2, 4096, 1024, 0));
  CALL((stopMixerCapture(), 0));
  ERR(startMixerCapture(0, 12345, 2, 4096, 1024, 0)); // not the engine rate
  ERR(startMixerCapture(0, 44100, 7, 4096, 1024, 0)); // not the engine's
  ERR(startMixerCapture(0, 44100, 2, 0, 1024, 0));
  ERR(startMixerCapture(0, 44100, 2, -4096, 1024, 0));
  ERR(startMixerCapture(0, 44100, 2, 4096, -1, -5));
  CALL((stopMixerCapture(), 0));
}
CASE("startMixerCaptureOnPort") {
  unsigned int id = 0;
  ERR(startMixerCaptureOnPort(0, 44100, 2, 4096, 1024, 0, nullptr, nullptr, 0,
                              -1));
  ERR(startMixerCaptureOnPort(0, 44100, 2, 4096, 1024, 0, &id, nullptr, 0,
                              -1));
  ERR(startMixerCaptureOnPort(-1, 44100, 2, -1, 1024, 0, &id, nullptr, 5,
                              -1));
  CALL((stopMixerCapture(), 0));
}
CASE("stopMixerCapture") {
  CALL((stopMixerCapture(), 0));
  CALL((stopMixerCapture(), 0));
}
CASE("stopMixerCaptureAndDrain") {
  size_t chunk = 0, len = 0;
  ZERO(stopMixerCaptureAndDrain(0, nullptr, nullptr));
  ZERO(stopMixerCaptureAndDrain(0x0BAD0BAD, &chunk, &len));
  ZERO(stopMixerCaptureAndDrain(0xFFFFFFFF, nullptr, &len));
}
CASE("takeMixerOutputPacket") {
  unsigned int id = 0;
  size_t len = 0;
  ZERO(takeMixerOutputPacket(nullptr, nullptr, nullptr));
  ZERO(takeMixerOutputPacket(nullptr, &id, &len));
  ZERO(takeMixerOutputPacket(reinterpret_cast<unsigned char *>(0x0BAD0BAD),
                             &id, &len));
  ZERO(takeMixerOutputPacket(reinterpret_cast<unsigned char *>(0x0BAD0BAD),
                             nullptr, nullptr));
}
CASE("mixerCaptureGetters") {
  CALL(getMixerCaptureId());
  ZERO(isMixerCaptureRunning());
  ZERO(getMixerCaptureBufferPointer());
  ZERO(getMixerCaptureBufferSize());
  ZERO(getMixerCaptureAvailableBytes());
  CALL(getMixerCaptureReadOffset());
}
CASE("advanceMixerCaptureReadPosition") {
  CALL((advanceMixerCaptureReadPosition(0), 0));
  CALL((advanceMixerCaptureReadPosition(-1), 0));
  CALL((advanceMixerCaptureReadPosition(1 << 30), 0));
  CALL((advanceMixerCaptureReadPosition(std::numeric_limits<int>::min()), 0));
}
CASE("getMixerOutputWavHeader") { CALL(getMixerOutputWavHeader()); }
CASE("setMixerOutputCallback") {
  CALL(setMixerOutputCallbackForEngine(nullptr, -1));
  CALL(setMixerOutputCallbackForEngine(nullptr, 424242));
  CALL((setMixerOutputCallback(nullptr), 0));
}
CASE("setVisualizationCallback") {
  CALL(setVisualizationCallbackForEngine(nullptr, -1));
  CALL(setVisualizationCallbackForEngine(nullptr, 424242));
  CALL((setVisualizationCallback(nullptr), 0));
}
CASE("visualizationPacketDone") {
  CALL((visualizationPacketDone(nullptr), 0));
  // Not tested: a pointer the engine never handed out. The function frees it
  // (documented: it takes back a packet the visualization callback gave).
}
CASE("areXiphLibsAvailable") { CALL(areXiphLibsAvailable()); }

// --- init / device ----------------------------------------------------------
CASE("initEngine") {
  // While inited a second init must say so; before / after, nonsense
  // parameters must be refused without opening anything.
  if (inited()) {
    ERR(initEngine(-1, 44100, 2048, 2, 0, 0, 0));
  } else {
    ERR(initEngine(0x7FFFFFFF, 0, 0, 0, 0, 0, 0));
  }
}
CASE("initEngineBadParams") {
  if (inited()) return;
  prepareEngineInit(-1);
  // Each of these is invalid; none may crash. Whatever it returns, tear it
  // down again.
  // 0 channels means "the device's": noError is fine.
  step("initEngine(-1, 44100, 2048, 0 channels)");
  (void)initEngine(-1, 44100, 2048, 0, 0, 0, 0);
  dispose();
  prepareEngineInit(-1);
  step("initEngine(-1, 44100, 2048, 1000 channels)");
  if (initEngine(-1, 44100, 2048, 1000, 0, 0, 0) == noError)
    failNoError("initEngine with 1000 channels returned noError");
  dispose();
  prepareEngineInit(-1);
  step("initEngine(-1, 0 Hz, 0 buffer)");
  PlayerErrors r = initEngine(-1, 0, 0, 2, 0, 0, 0);
  (void)r; // 0 means "device default" for both: noError is fine.
  dispose();
}
CASE("setAndroidAAudioAttributes") {
  CALL((setAndroidAAudioAttributes(0xFFFFFFFF), 0));
}
CASE("setLinuxAudioBackend") {
  // A no-op off Linux; an out-of-range backend must still not crash.
  CALL(setLinuxAudioBackend(0xFFFFFFFF));
}
CASE("setAudioDeviceIdleTimeout") {
  CALL((setAudioDeviceIdleTimeout(-1), 0));
  CALL((setAudioDeviceIdleTimeout(std::numeric_limits<int64_t>::min()), 0));
  CALL((setAudioDeviceIdleTimeout(std::numeric_limits<int64_t>::max()), 0));
  CALL((setAudioDeviceIdleTimeout(0), 0));
}
CASE("stopStartAudioDevice") {
  if (inited()) {
    CALL(stopAudioDevice(0xFFFFFFFF));
    CALL(startAudioDevice());
    CALL(startAudioDevice());
  } else {
    ERR(stopAudioDevice(0));
    ERR(stopAudioDevice(1));
    ERR(startAudioDevice());
  }
}
CASE("getAudioDeviceState") { CALL(getAudioDeviceState()); }
CASE("debugTriggerAudioInterruption") {
  CALL((debugTriggerAudioInterruption(1), 0));
  CALL((debugTriggerAudioInterruption(0), 0));
  CALL((debugTriggerAudioInterruption(0xFFFFFFFF), 0));
}
CASE("listPlaybackDevices") {
  char *names = nullptr;
  int *ids = nullptr, *defs = nullptr;
  int n = 0;
  CALL((listPlaybackDevicesBounded(nullptr, nullptr, nullptr, nullptr, 0), 0));
  CALL((listPlaybackDevicesBounded(&names, &ids, &defs, &n, 0), 0));
  CALL((listPlaybackDevicesBounded(&names, &ids, &defs, &n, -5), 0));
  CALL((listPlaybackDevices(nullptr, nullptr, nullptr, nullptr), 0));
}
CASE("changeDevice") {
  ERR(changeDevice(-2));
  ERR(changeDevice(0x7FFFFFFF));
}
CASE("freeListPlaybackDevices") {
  CALL((freeListPlaybackDevices(nullptr, nullptr, nullptr, 0), 0));
  CALL((freeListPlaybackDevices(nullptr, nullptr, nullptr, -1), 0));
  CALL((freeListPlaybackDevices(nullptr, nullptr, nullptr, 5), 0));
}
CASE("dispose") {
  CALL((dispose(), 0));
  CALL((dispose(), 0));
  CALL(isInited());
}
CASE("lifecycleRequests") {
  CALL(currentEngineShutdownEpoch());
  CALL(prepareEngineInitForRequest(-7, 0xFFFFFFFFFFFFFFFFull));
  CALL(requestEngineTeardownForEngine(-7));
  CALL(requestEngineTeardownForEngine(0x7FFFFFFFFFFFFFFFll));
  CALL((requestEngineShutdown(), 0));
  CALL((prepareEngineInit(-1), 0));
}
CASE("isInited") { CALL(isInited()); }

// --- loading ----------------------------------------------------------------
CASE("loadFile") {
  CALL((loadFile(nullptr, true, 0), 0));
  CALL((loadFile(nullptr, false, 1), 0));
  char empty[] = "";
  CALL((loadFile(empty, true, 2), 0));
  char missing[] = "/nonexistent/dir/missing.wav";
  CALL((loadFile(missing, false, 3), 0));
}
CASE("loadMem") {
  unsigned int hash = 0;
  char name[] = "bad.wav";
  unsigned char junk[16] = {1, 2, 3};
  ERR(loadMem(nullptr, gWav.data(), (int)gWav.size(), 1, &hash));
  ERR(loadMem(name, nullptr, (int)gWav.size(), 1, &hash));
  ERR(loadMem(name, nullptr, 0, 1, &hash));
  ERR(loadMem(name, gWav.data(), 0, 1, &hash));
  ERR(loadMem(name, gWav.data(), -1, 1, &hash));
  ERR(loadMem(name, gWav.data(), std::numeric_limits<int>::min(), 0, &hash));
  ERR(loadMem(name, junk, sizeof(junk), 1, &hash));
  ERR(loadMem(name, junk, sizeof(junk), 0, &hash));
  ERR(loadMem(name, gWav.data(), (int)gWav.size(), 1, nullptr));
  ERR(loadMem(name, gWav.data(), 30, 1, &hash)); // truncated header
}
CASE("joinTwoSources") {
  unsigned int hash = 0;
  char name[] = "joined";
  ERR(joinTwoSources(nullptr, gWav.data(), gWav.data(), (int)gWav.size(),
                     (int)gWav.size(), &hash));
  ERR(joinTwoSources(name, nullptr, nullptr, 10, 10, &hash));
  ERR(joinTwoSources(name, gWav.data(), nullptr, (int)gWav.size(), 10, &hash));
  ERR(joinTwoSources(name, gWav.data(), gWav.data(), -1, -1, &hash));
  ERR(joinTwoSources(name, gWav.data(), gWav.data(), 0, 0, &hash));
  ERR(joinTwoSources(name, gWav.data(), gWav.data(), (int)gWav.size(),
                     (int)gWav.size(), nullptr));
}

// --- buffer streams ---------------------------------------------------------
CASE("setBufferStream") {
  unsigned int hash = 0;
  ERR(setBufferStream(nullptr, 1 << 20, 0, 1.0, 44100, 2, 0, nullptr,
                      nullptr));
  ERR(setBufferStream(&hash, 0, 0, 1.0, 44100, 2, 0, nullptr, nullptr));
  ERR(setBufferStream(&hash, 1 << 20, -1, 1.0, 44100, 2, 0, nullptr, nullptr));
  ERR(setBufferStream(&hash, 1 << 20, 0, -1.0, 0, 0, 0, nullptr, nullptr));
  ERR(setBufferStream(&hash, 1 << 20, 0, 1.0, 44100, 2, 99, nullptr,
                      nullptr));
  ERR(setBufferStream(&hash, 1 << 20, 0, 1.0, 44100, 2, -1, nullptr,
                      nullptr));
  ERR(setBufferStream(&hash, 1 << 20, 0, kNaN, 44100, 1000, 0, nullptr,
                      nullptr));
}
CASE("bufferStreamOps") {
  float t = 0;
  unsigned int size = 0;
  unsigned char data[64] = {};
  const unsigned int wav = validSound();
  FOR_HASHES {
    ERR(resetBufferStream(s));
    ERR(getStreamTimeConsumed(s, &t));
    ERR(getStreamTimeConsumed(s, nullptr));
    ERR(setBufferIcyMetaInt(s, -1));
    ERR(addAudioDataStream(s, data, sizeof(data)));
    ERR(addAudioDataStream(s, nullptr, 100));
    ERR(setDataIsEnded(s));
    ERR(getBufferSize(s, &size));
    ERR(getBufferSize(s, nullptr));
  }
  if (wav != 0) {
    ctx("wav hash");
    // A valid hash that is not a buffer stream.
    ERR(resetBufferStream(wav));
    ERR(getStreamTimeConsumed(wav, &t));
    ERR(setBufferIcyMetaInt(wav, 16000));
    ERR(addAudioDataStream(wav, data, sizeof(data)));
    ERR(setDataIsEnded(wav));
    ERR(getBufferSize(wav, &size));
  }
}
CASE("bufferStreamValidBadArgs") {
  if (!inited()) return;
  unsigned int hash = 0;
  step("setBufferStream valid");
  if (setBufferStream(&hash, 1 << 16, 0, 0.1, 44100, 2, 0, nullptr,
                      nullptr) != noError)
    return;
  ctx("buffer stream");
  ERR(addAudioDataStream(hash, nullptr, 64));
  CALL(addAudioDataStream(hash, nullptr, 0));
  ERR(getStreamTimeConsumed(hash, nullptr));
  ERR(getBufferSize(hash, nullptr));
  CALL(setBufferIcyMetaInt(hash, -5)); // negative is clamped to 0
  CALL(setDataIsEnded(hash));
  CALL(setDataIsEnded(hash));
  unsigned char d[8] = {};
  ERR(addAudioDataStream(hash, d, sizeof(d))); // after the end
}
CASE("setPullBufferStream") {
  unsigned int hash = 0;
  ERR(setPullBufferStream(nullptr, 4096, 0.5, 44100, 2, 0, 1000, nullptr,
                          nullptr, nullptr, nullptr));
  ERR(setPullBufferStream(&hash, 0, 0.5, 44100, 2, 0, 1000, nullptr, nullptr,
                          nullptr, nullptr));
  ERR(setPullBufferStream(&hash, 4096, -1.0, 0, 0, 0, 0, nullptr, nullptr,
                          nullptr, nullptr));
  ERR(setPullBufferStream(&hash, 4096, kNaN, 44100, 2, 99, 1000, nullptr,
                          nullptr, nullptr, nullptr));
  ERR(setPullBufferStream(&hash, 4096, 0.5, 44100, 2, -1, 1000, nullptr,
                          nullptr, nullptr, nullptr));
}
CASE("pullBufferStreamOps") {
  double a = 0, b2 = 0;
  unsigned char data[64] = {};
  FOR_HASHES {
    ERR(resetPullBufferStream(s));
    ERR(addPullBufferDataStream(s, data, sizeof(data), 0));
    ERR(addPullBufferDataStream(s, nullptr, 64, 0));
    ERR(addPullBufferDataStream(s, data, 0, ~0ull));
    ERR(getPullBufferTimeRange(s, &a, &b2));
    ERR(getPullBufferTimeRange(s, nullptr, nullptr));
  }
}
CASE("pullBufferStreamValidBadArgs") {
  if (!inited()) return;
  unsigned int hash = 0;
  step("setPullBufferStream valid");
  if (setPullBufferStream(&hash, 1 << 16, 0.5, 44100, 2, 0, 1 << 20, nullptr,
                          nullptr, nullptr, nullptr) != noError)
    return;
  ctx("pull stream");
  ERR(addPullBufferDataStream(hash, nullptr, 64, 0));
  CALL(getPullBufferTimeRange(hash, nullptr, nullptr)); // nulls are skipped
  unsigned char d[16] = {};
  CALL(addPullBufferDataStream(hash, d, sizeof(d), ~0ull));
  CALL(addPullBufferDataStream(hash, d, sizeof(d), (1ull << 20) - 4));
}

// --- waveforms / speech -----------------------------------------------------
CASE("loadWaveform") {
  unsigned int hash = 0;
  ERR(loadWaveform(0, false, 1.0f, 0.0f, nullptr));
  // Out-of-range waveform ids play as a square wave (generateWaveform's
  // default): accepted.
  CALL(loadWaveform(-1, false, 1.0f, 0.0f, &hash));
  CALL(loadWaveform(9999, true, kNaN, kInf, &hash));
}
CASE("waveformSetters") {
  FOR_HASHES {
    CALL((setWaveformScale(s, kNaN), 0));
    CALL((setWaveformDetune(s, -kInf), 0));
    CALL((setWaveformFreq(s, -1.0f), 0));
    CALL((setSuperWave(s, true), 0));
    CALL((setWaveform(s, -1), 0));
    CALL((setWaveform(s, 9999), 0));
  }
  const unsigned int wav = validSound();
  if (wav != 0) {
    // A valid hash that is not a waveform.
    ctx("wav hash");
    CALL((setWaveformScale(wav, 2.0f), 0));
    CALL((setWaveformDetune(wav, 2.0f), 0));
    CALL((setWaveformFreq(wav, 440.0f), 0));
    CALL((setSuperWave(wav, true), 0));
    CALL((setWaveform(wav, 1), 0));
  }
  if (inited()) {
    unsigned int wf = 0;
    step("loadWaveform valid");
    if (loadWaveform(0, false, 1.0f, 0.0f, &wf) == noError) {
      ctx("waveform");
      CALL((setWaveform(wf, -1), 0));
      CALL((setWaveform(wf, 0x7FFFFFFF), 0));
      CALL((setWaveformFreq(wf, kNaN), 0));
      CALL((setWaveformScale(wf, -kInf), 0));
    }
  }
}
CASE("speechText") {
  unsigned int handle = 0;
  char empty[] = "";
  ERR(speechText(nullptr, &handle));
  ERR(speechText(nullptr, nullptr));
  char text[] = "hi";
  if (inited())
    ERR(speechText(text, nullptr));
  else
    ERR(speechText(text, &handle));
  CALL(speechText(empty, &handle));
}

// --- voices -----------------------------------------------------------------
CASE("pauseAndSpeed") {
  FOR_HANDLES {
    ERR(pauseSwitch(h));
    ERR(setPause(h, true));
    CALL(getPause(h));
    CALL((setRelativePlaySpeed(h, kNaN), 0));
    CALL((setRelativePlaySpeed(h, -1.0f), 0));
    CALL(getRelativePlaySpeed(h));
  }
  const unsigned int v = validHandle();
  if (v != 0) {
    ctx("valid handle");
    CALL((setRelativePlaySpeed(v, kNaN), 0));
    CALL((setRelativePlaySpeed(v, 0.0f), 0));
    CALL((setRelativePlaySpeed(v, -kInf), 0));
  }
}
CASE("getApproximateVolume") {
  CALL(getApproximateVolume(0));
  CALL(getApproximateVolume(255));
  CALL(getApproximateVolume(0xFFFFFFFF));
}
CASE("play") {
  unsigned int handle = 0;
  FOR_HASHES {
    ERR(play(s, 0, 1, 0, false, false, 0, 0, -1, -1, 1, &handle));
    ERR(playClocked(s, 0.0, 0, 1, 0, 1, false, 0, 0, -1, -1, &handle));
    ERR(playScheduled(s, 0.0, 0.0, 0, 1, 0, 1, false, 0, 0, -1, -1, &handle));
    ERR(play3d(s, 0, 0, 0, 0, 0, 0, 0, 1, false, false, 0, &handle));
    ERR(play3dWithLoopPoints(s, 0, 0, 0, 0, 0, 0, 0, 1, false, false, 0, 0,
                             -1, -1, 1, &handle));
    ERR(play3dClocked(s, 0.0, 0, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0, -1, -1,
                      &handle));
    ERR(play3dScheduled(s, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0,
                        -1, -1, &handle));
  }
}
CASE("playValidHashBadArgs") {
  const unsigned int s = validSound();
  if (s == 0) return;
  unsigned int handle = 0;
  ctx("valid hash");
  ERR(play(s, 0, 1, 0, true, false, 0, 0, -1, -1, 1, nullptr));
  ERR(playClocked(s, 0.0, 0, 1, 0, 1, false, 0, 0, -1, -1, nullptr));
  ERR(playScheduled(s, 0.0, 0.0, 0, 1, 0, 1, false, 0, 0, -1, -1, nullptr));
  ERR(play3d(s, 0, 0, 0, 0, 0, 0, 0, 1, true, false, 0, nullptr));
  ERR(play3dWithLoopPoints(s, 0, 0, 0, 0, 0, 0, 0, 1, true, false, 0, 0, -1,
                           -1, 1, nullptr));
  ERR(play3dClocked(s, 0.0, 0, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0, -1, -1,
                    nullptr));
  ERR(play3dScheduled(s, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0,
                      -1, -1, nullptr));
  FOR_BUSES {
    ERR(play(s, b, 1, 0, true, false, 0, 0, -1, -1, 1, &handle));
    ERR(playClocked(s, 0.0, b, 1, 0, 1, false, 0, 0, -1, -1, &handle));
    ERR(playScheduled(s, 0.0, 0.0, b, 1, 0, 1, false, 0, 0, -1, -1, &handle));
    ERR(play3d(s, b, 0, 0, 0, 0, 0, 0, 1, true, false, 0, &handle));
    ERR(play3dClocked(s, 0.0, b, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0, -1, -1,
                      &handle));
    ERR(play3dScheduled(s, 0.0, 0.0, b, 0, 0, 0, 0, 0, 0, 1, 1, false, 0, 0,
                        -1, -1, &handle));
  }
  ctx("valid hash, odd values");
  // Odd but not invalid values: they may play, but must not crash.
  CALL(play(s, 0, kNaN, kNaN, true, true, -1.0, -5.0, -7, -9, kNaN, &handle));
  CALL(play(s, 0, kInf, -kInf, true, true, 1e30, 1e-30, 0x7FFFFFFF,
            0x7FFFFFFF, -1.0f, &handle));
  CALL(playClocked(s, kNaN, 0, 1, 0, 0.0f, true, kNaN, kNaN, -1, -1,
                   &handle));
  CALL(playScheduled(s, -1.0, -1.0, 0, 1, 0, 1, false, 0, 0, -1, -1,
                     &handle));
  CALL(playScheduled(s, kNaN, kNaN, 0, 1, 0, 1, false, 0, 0, -1, -1,
                     &handle));
  CALL(play3d(s, 0, kNaN, kNaN, kNaN, kInf, kInf, kInf, kNaN, true, true,
              kNaN, &handle));
  CALL(play3dScheduled(s, kNaN, kNaN, 0, kNaN, 0, 0, 0, 0, 0, 1, 0.0f, true,
                       -1, -1, -1, -1, &handle));
  CALL((stopAll(), 0));
}
CASE("voiceTimeAndSchedule") {
  FOR_HANDLES {
    CALL((setDelaySamples(h, 0xFFFFFFFF), 0));
    CALL(getStreamTime(h));
    CALL((stopScheduled(h, kNaN), 0));
    CALL((fadeScheduled(h, -1.0, kNaN, -1.0, true), 0));
  }
  CALL((resetStreamTime(), 0));
  CALL(getEngineTime());
  CALL(getPlayheadTime());
  CALL(getOutputLatency());
  CALL(getHardwareOutputLatency());
  CALL(isRenderAheadEnabled());
}
CASE("stop") {
  FOR_HANDLES { ERR(stop(h)); }
  CALL((stopAll(), 0));
  FOR_HASHES {
    CALL((stopAudioSource(s), 0));
    CALL((disposeSound(s), 0));
    ZERO(countAudioSource(s));
  }
  CALL((disposeAllSound(), 0));
  CALL((disposeAllSound(), 0));
}
CASE("disposeSoundTwice") {
  const unsigned int s = validSound();
  if (s == 0) return;
  ctx("valid hash");
  CALL((disposeSound(s), 0));
  CALL((disposeSound(s), 0));
  unsigned int handle = 0;
  ERR(play(s, 0, 1, 0, true, false, 0, 0, -1, -1, 1, &handle));
}
CASE("looping") {
  FOR_HANDLES {
    CALL(getLooping(h));
    CALL((setLooping(h, true), 0));
    CALL(getLoopPoint(h));
    CALL((setLoopPoint(h, kNaN), 0));
    CALL(getLoopEndPoint(h));
    CALL((setLoopEndPoint(h, -1.0), 0));
  }
  const unsigned int v = validHandle();
  if (v != 0) {
    ctx("valid handle");
    CALL((setLoopPoint(v, kNaN), 0));
    CALL((setLoopPoint(v, -kInf), 0));
    CALL((setLoopEndPoint(v, kNaN), 0));
    CALL((setLoopEndPoint(v, 1e300), 0));
  }
}
CASE("setVisualizationEnabled") {
  ERR(setVisualizationEnabled(true, -1, 0, 0));
  ERR(setVisualizationEnabled(true, 0, 0, 0));
  ERR(setVisualizationEnabled(true, 3, 0, 0)); // not a power of two
  ERR(setVisualizationEnabled(true, 1 << 30, 0, 0));
  ERR(setVisualizationEnabled(true, 256, -1, 0));
  ERR(setVisualizationEnabled(true, 256, 99, 0));
  ERR(setVisualizationEnabled(true, 256, 0, -3)); // -1, -2: merged / all
  ERR(setVisualizationEnabled(true, 256, 0, 1000));
  CALL(getVisualizationEnabled());
  CALL((setFftSmoothing(kNaN), 0));
  CALL((setFftSmoothing(-5.0f), 0));
  CALL((setFftDecibelRange(kNaN, kNaN), 0));
  CALL((setFftDecibelRange(10.0f, -10.0f), 0));
}
CASE("getLength") {
  FOR_HASHES { CALL(getLength(s)); }
}
CASE("seekAndPosition") {
  FOR_HANDLES {
    ERR(seek(h, 1.0f));
    ERR(seek(h, kNaN));
    CALL(getPosition(h));
  }
  const unsigned int v = validHandle();
  if (v != 0) {
    ctx("valid handle");
    CALL(seek(v, kNaN));
    CALL(seek(v, -1.0f));
    CALL(seek(v, kInf));
    CALL(seek(v, 1e30f));
  }
}
CASE("volumeAndPan") {
  CALL(getGlobalVolume());
  // Engine missing: must be an error. Inited: NaN / Inf / negative must not
  // be accepted silently either.
  ERR(setGlobalVolume(kNaN));
  ERR(setGlobalVolume(-kInf));
  FOR_HANDLES {
    CALL(getVolume(h));
    ERR(setVolume(h, 0.5f));
    CALL(getPan(h));
    CALL((setPan(h, kNaN), 0));
    CALL((setPanAbsolute(h, kNaN, -kInf), 0));
    ZERO(getIsValidVoiceHandle(h));
  }
  const unsigned int v = validHandle();
  if (v != 0) {
    ctx("valid handle");
    ERR(setVolume(v, kNaN));
    CALL((setPan(v, kNaN), 0));
    CALL((setPan(v, 1e30), 0));
    CALL((setPanAbsolute(v, kNaN, kInf), 0));
  }
}
CASE("voiceCounts") {
  CALL(getActiveVoiceCount());
  CALL(getVoiceCount());
  CALL(getMaxActiveVoiceCount());
  CALL((setMaxActiveVoiceCount(0), 0));
  CALL((setMaxActiveVoiceCount(0xFFFFFFFF), 0));
  CALL((setMaxActiveVoiceCount(16), 0));
}
CASE("protectAndInaudible") {
  FOR_HANDLES {
    CALL(getProtectVoice(h));
    CALL((setProtectVoice(h, true), 0));
    CALL((setInaudibleBehavior(h, true, true), 0));
  }
}
CASE("voiceGroups") {
  FOR_HANDLES {
    CALL((destroyVoiceGroup(h), 0));
    CALL((addVoiceToGroup(h, h), 0));
    ZERO(isVoiceGroup(h));
    CALL(isVoiceGroupEmpty(h));
  }
  if (inited()) {
    step("createVoiceGroup");
    const unsigned int g = createVoiceGroup();
    if (g != 0) {
      ctx("group");
      CALL((addVoiceToGroup(g, 0x0BAD0BAD), 0));
      CALL((addVoiceToGroup(g, 0), 0));
      CALL((addVoiceToGroup(g, g), 0)); // a group into itself
      CALL((destroyVoiceGroup(g), 0));
      CALL((destroyVoiceGroup(g), 0));
      CALL((addVoiceToGroup(g, 0x0BAD0BAD), 0));
    }
  } else {
    ZERO(createVoiceGroup());
  }
}
CASE("fadesAndOscillators") {
  ERR(fadeGlobalVolume(kNaN, kNaN));
  ERR(oscillateGlobalVolume(kNaN, kNaN, -1.0f));
  FOR_HANDLES {
    ERR(fadeVolume(h, 0.5f, 1.0f));
    ERR(fadePan(h, 0.5f, 1.0f));
    ERR(fadeRelativePlaySpeed(h, 0.5f, 1.0f));
    ERR(schedulePause(h, 1.0f));
    ERR(scheduleStop(h, 1.0f));
    ERR(oscillateVolume(h, 0, 1, 1));
    ERR(oscillatePan(h, 0, 1, 1));
    ERR(oscillateRelativePlaySpeed(h, 0.5f, 1, 1));
  }
  const unsigned int v = validHandle();
  if (v != 0) {
    ctx("valid handle");
    CALL(fadeVolume(v, kNaN, kNaN));
    CALL(fadeVolume(v, 1, -1));
    CALL(fadePan(v, kInf, 0));
    CALL(fadeRelativePlaySpeed(v, -1, kNaN));
    CALL(schedulePause(v, kNaN));
    CALL(scheduleStop(v, -kInf));
    CALL(oscillateVolume(v, kNaN, kNaN, 0));
    CALL(oscillatePan(v, 0, 1, kNaN));
    CALL(oscillateRelativePlaySpeed(v, 0, 0, 0));
  }
}

// --- filters ----------------------------------------------------------------
CASE("filterParamNames") {
  int count = 0;
  char *names[32] = {};
  // An unknown type has no parameters: count 0, noError when inited.
  if (inited()) {
    CALL(getFilterParamNames((FilterType)-1, &count, names));
    CALL(getFilterParamNames((FilterType)99, &count, names));
    CALL(getFilterParamNamesBounded((FilterType)99, &count, names, 32));
  } else {
    ERR(getFilterParamNames((FilterType)-1, &count, names));
    ERR(getFilterParamNamesBounded((FilterType)99, &count, names, 32));
  }
  ERR(getFilterParamNames(EchoFilter, nullptr, names));
  ERR(getFilterParamNames(EchoFilter, &count, nullptr));
  ERR(getFilterParamNamesBounded(EchoFilter, &count, names, -1));
  ERR(getFilterParamNamesBounded(EchoFilter, nullptr, nullptr, 32));
  // capacity 0 / 1: must stop at the capacity, not write past it.
  char *one[1] = {};
  CALL(getFilterParamNamesBounded(EchoFilter, &count, names, 0));
  CALL(getFilterParamNamesBounded(ParametricEQFilter, &count, one, 1));
}
CASE("filters") {
  int index = 0;
  float value = 0;
  // Out-of-range types travel as ints, as they do from Dart.
  // Out-of-range types, once each per entry point.
  for (int bad : {-1, 99}) {
    ctx("type=%d", bad);
    const auto bt = static_cast<FilterType>(bad);
    ERR(isFilterActive(0, 0, bt, &index));
    ERR(addFilter(0, 0, bt));
    ERR(removeFilter(0, 0, bt));
    ERR(setFilterParams(0, 0, bt, 0, 1.0f));
    ERR(getFilterParams(0, 0, bt, 0, &value));
    ERR(fadeFilterParameter(0, 0, bt, 0, 1, 1));
    ERR(oscillateFilterParameter(0, 0, bt, 0, 0, 1, 1));
  }
  // Bad hashes / buses / handles with a valid type.
  for (int fti : {(int)EchoFilter}) {
    const auto ft = static_cast<FilterType>(fti);
    ctx("type=%d", fti);
    FOR_HASHES {
      ctx("type=%d hash=0x%x", fti, s);
      if (s != 0) {
        ERR(isFilterActive(s, 0, ft, &index));
        ERR(addFilter(s, 0, ft));
        ERR(removeFilter(s, 0, ft));
      }
    }
    FOR_BUSES {
      ctx("type=%d bus=0x%x", fti, b);
      ERR(isFilterActive(0, b, ft, &index));
      ERR(addFilter(0, b, ft));
      ERR(removeFilter(0, b, ft));
      ERR(setFilterParams(0, b, ft, 0, 1.0f));
      ERR(getFilterParams(0, b, ft, 0, &value));
      ERR(fadeFilterParameter(0, b, ft, 0, 1, 1));
      ERR(oscillateFilterParameter(0, b, ft, 0, 0, 1, 1));
    }
    ctx("type=%d global", fti);
    ERR(isFilterActive(0, 0, ft, nullptr));
    FOR_HANDLES {
      ctx("type=%d h=0x%x", fti, h);
      ERR(setFilterParams(h, 0, ft, -1, 1.0f));
      ERR(setFilterParams(h, 0, ft, 9999, kNaN));
      ERR(getFilterParams(h, 0, ft, 0, nullptr));
      ERR(getFilterParams(h, 0, ft, -1, &value));
      ERR(fadeFilterParameter(h, 0, ft, -1, 1, 1));
      ERR(oscillateFilterParameter(h, 0, ft, 9999, 0, 1, kNaN));
    }
  }
}
CASE("globalFilterBadParams") {
  if (!inited()) return;
  float value = 0;
  step("addFilter(0, 0, EchoFilter)");
  if (addFilter(0, 0, EchoFilter) != noError) return;
  ctx("echo active");
  ERR(addFilter(0, 0, EchoFilter)); // already added
  ERR(setFilterParams(0, 0, EchoFilter, -1, 1.0f));
  ERR(setFilterParams(0, 0, EchoFilter, 9999, 1.0f));
  ERR(getFilterParams(0, 0, EchoFilter, -1, &value));
  ERR(getFilterParams(0, 0, EchoFilter, 9999, &value));
  ERR(getFilterParams(0, 0, EchoFilter, 0, nullptr));
  ERR(fadeFilterParameter(0, 0, EchoFilter, 9999, 1, 1));
  ERR(oscillateFilterParameter(0, 0, EchoFilter, -1, 0, 1, 1));
  CALL(setFilterParams(0, 0, EchoFilter, 0, kNaN));
  CALL(fadeFilterParameter(0, 0, EchoFilter, 0, kNaN, kNaN));
  CALL(removeFilter(0, 0, EchoFilter));
  ERR(removeFilter(0, 0, EchoFilter)); // not active anymore
}

// --- realtime streams -------------------------------------------------------
CASE("setRealtimeStream") {
  unsigned int hash = 0;
  ERR(setRealtimeStream(nullptr, 44100, 2, 0, 4096, 0));
  ERR(setRealtimeStream(&hash, 0, 2, 0, 4096, 0));
  ERR(setRealtimeStream(&hash, 44100, 0, 0, 4096, 0));
  ERR(setRealtimeStream(&hash, 44100, 1000, 0, 4096, 0));
  ERR(setRealtimeStream(&hash, 44100, 2, -1, 4096, 0));
  ERR(setRealtimeStream(&hash, 44100, 2, 99, 4096, 0));
  ERR(setRealtimeStream(&hash, 44100, 2, 0, 0xFFFFFFFF, 0xFFFFFFFF));
}
CASE("realtimeStreamOps") {
  float f[64] = {};
  int16_t i16[64] = {};
  const unsigned int wav = validSound();
  std::vector<unsigned int> hashes(std::begin(kBadHashes),
                                   std::end(kBadHashes));
  if (wav != 0) hashes.push_back(wav);
  for (unsigned int s : hashes) {
    ctx("hash=0x%x", s);
    ERR(resetRealtimeStream(s));
    ZERO(pushRealtimeFloat32(s, f, 32, 64));
    ZERO(pushRealtimeFloat32(s, nullptr, 32, 64));
    ZERO(pushRealtimeInt16(s, i16, 32, 64));
    ZERO(pushRealtimeInt16(s, nullptr, 32, 64));
    ZERO(pushRealtimeClockedFloat32(s, kNaN, f, 32, 64));
    ZERO(pushRealtimeScheduledFloat32(s, -1.0, nullptr, 32, 64));
    ZERO(getRealtimeAvailableToRead(s));
    ZERO(getRealtimeAvailableToWrite(s));
    ZERO(getRealtimeUnderrunCount(s));
  }
}
CASE("realtimeStreamValidBadArgs") {
  if (!inited()) return;
  unsigned int hash = 0;
  step("setRealtimeStream valid");
  if (setRealtimeStream(&hash, 44100, 2, 0, 4096, 1024) != noError) return;
  ctx("realtime stream");
  float f[64] = {};
  int16_t i16[64] = {};
  ZERO(pushRealtimeFloat32(hash, nullptr, 32, 64));
  ZERO(pushRealtimeInt16(hash, nullptr, 32, 64));
  ZERO(pushRealtimeFloat32(hash, f, 0, 0));
  // numSamples smaller than numFrames * channels: the stream must not read
  // past the caller's buffer.
  ZERO(pushRealtimeFloat32(hash, f, 1000000, 64));
  ZERO(pushRealtimeInt16(hash, i16, 1000000, 64));
  ZERO(pushRealtimeClockedFloat32(hash, 0.0, f, 1000000, 64));
  ZERO(pushRealtimeScheduledFloat32(hash, 0.0, f, 1000000, 64));
  CALL(pushRealtimeClockedFloat32(hash, kNaN, f, 32, 64));
  CALL(pushRealtimeScheduledFloat32(hash, kNaN, f, 32, 64));
  CALL(pushRealtimeScheduledFloat32(hash, -kInf, f, 32, 64));
}

// --- custom sources / filters -----------------------------------------------
CASE("createCustomAudioSource") {
  unsigned int hash = 0;
  ERR(createCustomAudioSource(nullptr, 44100, 2, nullptr, nullptr, nullptr,
                              false, nullptr, false, false));
  if (!inited()) {
    ERR(createCustomAudioSource(&hash, 44100, 2, nullptr, nullptr, nullptr,
                                false, nullptr, false, false));
  }
  ERR(createCustomAudioSource(&hash, 44100, 1000, nullptr, nullptr, nullptr,
                              false, nullptr, false, false));
}
CASE("customFilters") {
  int index = 0;
  float value = 0;
  for (int id : {-1, 8, 0x7FFFFFFF, std::numeric_limits<int>::min()}) {
    ctx("id=%d global", id);
    ERR(addCustomFilter(0, 0, id));
    ERR(removeCustomFilter(0, 0, id));
    ERR(isCustomFilterActive(0, 0, id, &index));
    ERR(setCustomFilterCallback(0, 0, id, nullptr, nullptr, 2, nullptr));
    ERR(setCustomFilterParamDefinition(0, 0, id, 1, "x", 0, 1, 0));
    ERR(setCustomFilterParams(0, 0, id, 1, 1.0f));
    ERR(getCustomFilterParams(0, 0, id, 1, &value));
    ERR(fadeCustomFilterParameter(0, 0, id, 1, 1, 1));
    ERR(oscillateCustomFilterParameter(0, 0, id, 1, 0, 1, 1));
  }
  FOR_HASHES {
    if (s == 0) continue;
    ERR(addCustomFilter(s, 0, 0));
    ERR(removeCustomFilter(s, 0, 0));
    ERR(isCustomFilterActive(s, 0, 0, &index));
    ERR(setCustomFilterCallback(s, 0, 0, nullptr, nullptr, 2, nullptr));
    ERR(setCustomFilterParamDefinition(s, 0, 0, 1, nullptr, 0, 1, 0));
  }
  FOR_BUSES {
    ERR(addCustomFilter(0, b, 0));
    ERR(removeCustomFilter(0, b, 0));
    ERR(isCustomFilterActive(0, b, 0, &index));
    ERR(setCustomFilterCallback(0, b, 0, nullptr, nullptr, 2, nullptr));
    ERR(setCustomFilterParamDefinition(0, b, 0, 1, nullptr, 0, 1, 0));
    ERR(setCustomFilterParams(0, b, 0, 1, 1.0f));
    ERR(getCustomFilterParams(0, b, 0, 1, &value));
    ERR(fadeCustomFilterParameter(0, b, 0, 1, 1, 1));
    ERR(oscillateCustomFilterParameter(0, b, 0, 1, 0, 1, 1));
  }
  FOR_HANDLES {
    if (h == 0) continue;
    ERR(setCustomFilterParams(h, 0, 0, 1, 1.0f));
    ERR(getCustomFilterParams(h, 0, 0, 1, &value));
    ERR(fadeCustomFilterParameter(h, 0, 0, 1, 1, 1));
    ERR(oscillateCustomFilterParameter(h, 0, 0, 1, 0, 1, 1));
  }
  ctx("global, slot 0");
  ERR(isCustomFilterActive(0, 0, 0, nullptr));
  ERR(getCustomFilterParams(0, 0, 0, 1, nullptr));
  ERR(setCustomFilterParamDefinition(0, 0, 0, 16, "x", 0, 1, 0));
  ERR(setCustomFilterParamDefinition(0, 0, 0, 0xFFFFFFFF, "x", 0, 1, 0));
  ERR(setCustomFilterParams(0, 0, 0, -1, 1.0f));
  ERR(setCustomFilterParams(0, 0, 0, 16, 1.0f));
  ERR(getCustomFilterParams(0, 0, 0, 99, &value));
  ERR(fadeCustomFilterParameter(0, 0, 0, -1, 1, 1));
  ERR(oscillateCustomFilterParameter(0, 0, 0, 99, 0, 1, 1));
}
CASE("customFilterValidBadArgs") {
  if (!inited()) return;
  float value = 0;
  step("addCustomFilter(0, 0, 0)");
  if (addCustomFilter(0, 0, 0) != noError) return;
  ctx("custom filter active");
  CALL(setCustomFilterCallback(0, 0, 0, nullptr, nullptr, 0, nullptr));
  CALL(setCustomFilterCallback(0, 0, 0, nullptr, nullptr, 0xFFFFFFFF, ""));
  ERR(setCustomFilterParams(0, 0, 0, 16, 1.0f));
  ERR(setCustomFilterParams(0, 0, 0, -1, 1.0f));
  ERR(getCustomFilterParams(0, 0, 0, 16, &value));
  ERR(getCustomFilterParams(0, 0, 0, 0, nullptr));
  CALL(setCustomFilterParams(0, 0, 0, 0, kNaN));
  CALL(setCustomFilterParamDefinition(0, 0, 0, 1, nullptr, kNaN, -kInf,
                                      kInf));
  CALL(removeCustomFilter(0, 0, 0));
  ERR(removeCustomFilter(0, 0, 0));
}

// --- 3D -----------------------------------------------------------------------
CASE("listener3d") {
  CALL((set3dSoundSpeed(kNaN), 0));
  CALL((set3dSoundSpeed(-1.0f), 0));
  CALL((set3dSoundSpeed(0.0f), 0));
  CALL(get3dSoundSpeed());
  CALL((set3dListenerParameters(kNaN, kNaN, kNaN, 0, 0, 0, 0, 0, 0, kInf,
                                kInf, kInf),
        0));
  CALL((set3dListenerPosition(kNaN, kInf, -kInf), 0));
  CALL((set3dListenerAt(0, 0, 0), 0));
  CALL((set3dListenerUp(0, 0, 0), 0));
  CALL((set3dListenerVelocity(kNaN, 0, 0), 0));
  set3dSoundSpeed(343.0f);
}
CASE("defaults3d") {
  FOR_HASHES {
    CALL((set3dDefaultMinMaxDistance(s, kNaN, kInf), 0));
    CALL((set3dDefaultMinMaxDistance(s, -1, -2), 0));
    CALL((set3dDefaultAttenuation(s, 0xFFFFFFFF, kNaN), 0));
    CALL((set3dDefaultAttenuation(s, 2, 1), 0));
    CALL((set3dDefaultDopplerFactor(s, kNaN), 0));
    CALL((set3dDefaultDopplerFactor(s, 0), 0));
  }
  const unsigned int wav = validSound();
  if (wav != 0) {
    CALL((set3dDefaultMinMaxDistance(wav, 0, 0), 0));
    CALL((set3dDefaultMinMaxDistance(wav, 10, 1), 0));
    CALL((set3dDefaultAttenuation(wav, 0xFFFFFFFF, -1), 0));
    CALL((set3dDefaultDopplerFactor(wav, -1), 0));
  }
}
CASE("update3d") {
  // Deferred mode: the setters store, update3dAudio() applies.
  CALL((set3dAutoUpdate(false), 0));
  CALL(get3dAutoUpdate());
  CALL((set3dListenerPosition(1, 2, 3), 0));
  CALL((update3dAudio(), 0));
  CALL((set3dAutoUpdate(true), 0));
  CALL(get3dAutoUpdate());
  CALL((update3dAudio(), 0));
}
CASE("source3d") {
  FOR_HANDLES {
    CALL((set3dSourceParameters(h, kNaN, 0, 0, 0, 0, 0), 0));
    CALL((set3dSourcePosition(h, 0, 0, 0), 0));
    CALL((set3dSourceVelocity(h, 0, 0, 0), 0));
    CALL((set3dSourceMinMaxDistance(h, -1, -2), 0));
    CALL((set3dSourceAttenuation(h, 0xFFFFFFFF, kNaN), 0));
    CALL((set3dSourceDopplerFactor(h, kNaN), 0));
  }
  if (!inited()) return;
  const unsigned int s = validSound();
  unsigned int v = 0;
  step("play3d valid");
  if (s == 0 ||
      play3d(s, 0, 1, 0, 0, 0, 0, 0, 1, false, true, 0, &v) != noError)
    return;
  ctx("valid 3d voice");
  CALL((set3dSourceMinMaxDistance(v, 0, 0), 0));
  CALL((set3dSourceMinMaxDistance(v, 10, 1), 0));
  CALL((set3dSourceAttenuation(v, 0xFFFFFFFF, kNaN), 0));
  CALL((set3dSourcePosition(v, kNaN, kNaN, kNaN), 0));
  CALL((set3dSourceDopplerFactor(v, kInf), 0));
  CALL((set3dSoundSpeed(0.0f), 0));
  CALL((set3dListenerAt(0, 0, 0), 0));
  CALL((set3dListenerUp(0, 0, 0), 0));
  usleep(200000); // let the device thread run the 3D update a few times
  CALL(getIsValidVoiceHandle(v));
}

// --- read samples -------------------------------------------------------------
CASE("readSamples") {
  float out[64] = {};
  // No engine needed: readSamples* decode on their own. Bad input must not
  // be readSamplesNoError.
#define RERR(expr)                                                             \
  do {                                                                         \
    if (step(#expr) && (expr) == readSamplesNoError)                           \
      failNoError(#expr " returned NoError");                                  \
  } while (0)
  RERR(readSamplesFromFile(nullptr, 0, 1, 64, false, out));
  RERR(readSamplesFromFile("/nonexistent/x.wav", 0, 1, 64, false, out));
  RERR(readSamplesFromMem(nullptr, 100, 0, 1, 64, false, out));
  RERR(readSamplesFromMem(gWav.data(), 0, 0, 1, 64, false, out));
  RERR(readSamplesFromMem(gWav.data(), 10, 0, 1, 64, false, out));
  RERR(readSamplesFromMem(gWav.data(), gWav.size(), 0, 0.05f, 64, false,
                          nullptr));
  // Valid data, nonsense ranges.
  CALL(readSamplesFromMem(gWav.data(), gWav.size(), 0, 0.05f, 0, false, out));
  CALL(readSamplesFromMem(gWav.data(), gWav.size(), 0.05f, 0.0f, 64, true,
                          out));
  CALL(readSamplesFromMem(gWav.data(), gWav.size(), kNaN, kNaN, 64, true,
                          out));
  CALL(readSamplesFromMem(gWav.data(), gWav.size(), -1.0f, 100.0f, 64, false,
                          out));
  CALL(readSamplesFromMem(gWav.data(), gWav.size(), 0, 1e30f, 64, true, out));
}

// --- buses --------------------------------------------------------------------
CASE("buses") {
  unsigned int handle = 0;
  FOR_BUSES {
    CALL((destroyBus(b), 0));
    ERR(busPlayOnEngine(b, 1.0f, false, &handle));
    CALL(busSetChannels(b, 2));
    ERR(busSetVisualizationEnabled(b, true, 256, 0, 0));
    CALL(busGetVisualizationEnabled(b));
    CALL(busGetApproximateVolume(b, 0));
    CALL((busAnnexSound(b, 0x0BAD0BAD), 0));
    ZERO(busGetActiveVoiceCount(b));
  }
  ctx("");
  if (!inited()) {
    ZERO(createBus());
    return;
  }
  step("createBus");
  const unsigned int bus = createBus();
  if (bus == 0) return;
  ctx("valid bus");
  ERR(busPlayOnEngine(bus, 1.0f, false, nullptr));
  CALL(busSetChannels(bus, 0));
  CALL(busSetChannels(bus, 0xFFFFFFFF));
  ERR(busSetVisualizationEnabled(bus, true, -1, 0, 0));
  ERR(busSetVisualizationEnabled(bus, true, 256, 99, 0));
  ERR(busSetVisualizationEnabled(bus, true, 256, 0, 1000));
  CALL(busGetApproximateVolume(bus, 0xFFFFFFFF));
  FOR_HANDLES { CALL((busAnnexSound(bus, h), 0)); }
  ctx("valid bus");
  CALL((destroyBus(bus), 0));
  CALL((destroyBus(bus), 0));
  ERR(busPlayOnEngine(bus, 1.0f, false, &handle));
}

// --- VST3 (stubs unless built with SOLOUD_ENABLE_VST3) -----------------------
CASE("vst3") {
  int count = 0, plugin = 0;
  char **uids = nullptr, **names = nullptr, **cats = nullptr;
  unsigned char *state = nullptr;
  unsigned int size = 0, pid = 0, flags = 0;
  char title[8], units[8];
  double def = 0;
  int steps = 0;
  CALL(soloud_vst3IsSupported());
  ERR(soloud_vst3ScanBundle(nullptr, &count, &uids, &names, &cats));
  ERR(soloud_vst3ScanBundle("/nonexistent.vst3", nullptr, nullptr, nullptr,
                            nullptr));
  CALL((soloud_vst3FreeScanResults(0, nullptr, nullptr, nullptr), 0));
  CALL((soloud_vst3FreeScanResults(-1, nullptr, nullptr, nullptr), 0));
  CALL((soloud_vst3FreeScanResults(3, nullptr, nullptr, nullptr), 0));
  ERR(soloud_vst3LoadPlugin(nullptr, nullptr, 0, 0, 0, &plugin));
  ERR(soloud_vst3LoadPlugin("/nonexistent.vst3", "00", 44100, 512, 2,
                            nullptr));
  CALL(soloud_vst3GetFilterCallbackPtr());
  CALL(soloud_vst3GetSourceCallbackPtr());
  for (int p : {-1, 0, 0x0BAD0BAD}) {
    ctx("plugin=%d", p);
    ERR(soloud_vst3UnloadPlugin(p));
    ZERO(soloud_vst3GetPluginUserData(p));
    ZERO(soloud_vst3GetLatencySamples(p));
    ZERO(soloud_vst3GetParameterCount(p));
    ERR(soloud_vst3GetParameterInfo(p, 0, &pid, title, sizeof(title), units,
                                    sizeof(units), &def, &steps, &flags));
    ERR(soloud_vst3GetParameterInfo(p, -1, nullptr, nullptr, -1, nullptr, -1,
                                    nullptr, nullptr, nullptr));
    CALL(soloud_vst3GetParameterNormalized(p, 0, nullptr, -1));
    CALL(soloud_vst3GetParameterNormalized(p, 0, title, 0));
    ERR(soloud_vst3SetParameterNormalized(p, 0, kNaN));
    ERR(soloud_vst3MapSlotParameter(p, -1, 0));
    CALL((soloud_vst3SetTransport(p, kNaN, -1, 0, 1), 0));
    ERR(soloud_vst3SendMidiNote(p, 1, -1, 999, kNaN, -5));
    ERR(soloud_vst3GetState(p, &state, &size));
    ERR(soloud_vst3GetState(p, nullptr, nullptr));
    ERR(soloud_vst3SetState(p, nullptr, 100));
    ZERO(soloud_vst3HasEditor(p));
    ERR(soloud_vst3OpenEditor(p, nullptr));
    CALL((soloud_vst3CloseEditor(p), 0));
    ZERO(soloud_vst3IsEditorOpen(p));
  }
  CALL((soloud_vst3SetParameterEditCallback(nullptr), 0));
  CALL((soloud_vst3SetRestartComponentCallback(nullptr), 0));
  CALL((soloud_vst3HostUnloadAll(), 0));
}

// ---- known product bugs ------------------------------------------------------
// A failure whose text contains `match` (and, if set, happened in `phase`) is
// a known bug: reported as KNOWN BUG <id>, fails only with --strict.
struct KnownBug {
  const char *id;
  const char *match;  // must appear in the failure text
  const char *match2; // and this too, when not null
  const char *what;
};
// Empty while no bug is known: add {id, match, match2, what} entries here,
// specific ones first (the first match wins).
const std::vector<KnownBug> kKnownBugs = {};

const KnownBug *knownBug(const std::string &text) {
  for (const KnownBug &k : kKnownBugs) {
    if (text.find(k.match) == std::string::npos) continue;
    if (k.match2 != nullptr && text.find(k.match2) == std::string::npos)
      continue;
    return &k;
  }
  return nullptr;
}

void report(const std::string &text, int phase) {
  const KnownBug *k = knownBug(text);
  if (k != nullptr) {
    KNOWN_BUG(k->id, false, "[%s] %s -- %s", kPhaseName[phase], text.c_str(),
              k->what);
  } else {
    CHECK(false, "[%s] %s", kPhaseName[phase], text.c_str());
  }
}

// ---- child / parent ----------------------------------------------------------
std::string gLogPath;
std::set<std::string> gReported; // failures already reported for this case

[[noreturn]] void runChild(size_t index, int phase, int fd) {
  // The bindings print warnings and sanitizers print reports: keep them in a
  // log the parent shows only for a crash.
  FILE *log = std::fopen(gLogPath.c_str(), "w");
  if (log != nullptr) {
    dup2(fileno(log), 1);
    dup2(fileno(log), 2);
  }
  gPipe = fd;
  gPhase = phase;
  gCase = cases()[index].name;
  alarm(20);
  if (phase != kPre) {
    prepareEngineInit(-1);
    const PlayerErrors r = initEngine(-1, 44100, 2048, 2, 0, 0, 0);
    if (r != noError) {
      sendLine("X", "initEngine failed");
      _exit(3);
    }
    if (phase == kPost) dispose();
  }
  cases()[index].run();
  gCtx[0] = '\0';
  step("teardown");
  if (phase == kInited) dispose();
  sendLine("D", "done");
  _exit(0);
}

// Run the case once. Returns the number of the step that crashed or hung, 0
// when the child finished.
int runCaseOnce(size_t index, int phase) {
  int fds[2];
  if (pipe(fds) != 0) {
    CHECK(false, "pipe failed");
    return 0;
  }
  std::fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    runChild(index, phase, fds[1]);
  }
  close(fds[1]);
  std::string all;
  char buf[4096];
  ssize_t n;
  while ((n = read(fds[0], buf, sizeof(buf))) > 0) all.append(buf, n);
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);

  std::string lastStep;
  int lastStepNo = -1;
  bool done = false;
  size_t pos = 0;
  while (pos < all.size()) {
    size_t end = all.find('\n', pos);
    if (end == std::string::npos) end = all.size();
    const std::string line = all.substr(pos, end - pos);
    pos = end + 1;
    const size_t sp = line.find(' ');
    if (sp == std::string::npos) continue;
    const std::string body = line.substr(sp + 1);
    switch (line[0]) {
    case 'S':
      lastStep = body;
      lastStepNo = std::atoi(line.c_str() + 1);
      break;
    case 'F':
      if (gReported.insert(body).second) report(body, phase);
      break;
    case 'D': done = true; break;
    case 'X': CHECK(false, "[%s] %s", kPhaseName[phase], body.c_str()); break;
    }
  }
  ++robustness::gChecks;
  if (!done || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    char what[64];
    if (WIFSIGNALED(status))
      std::snprintf(what, sizeof(what), "%s (signal %d)",
                    WTERMSIG(status) == SIGALRM ? "HANG" : "CRASH",
                    WTERMSIG(status));
    else
      std::snprintf(what, sizeof(what), "CRASH (exit %d)",
                    WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    --robustness::gChecks; // report() counts it
    report(std::string(what) + " at " + lastStep, phase);
    // Show the sanitizer report / the first lines of the child's output.
    FILE *log = std::fopen(gLogPath.c_str(), "r");
    if (log != nullptr) {
      char line[512];
      int shown = 0;
      bool inReport = false;
      while (std::fgets(line, sizeof(line), log) != nullptr && shown < 25) {
        if (std::strstr(line, "ERROR:") || std::strstr(line, "runtime error"))
          inReport = true;
        if (inReport) {
          std::printf("      | %s", line);
          ++shown;
        }
      }
      std::fclose(log);
    }
    return lastStepNo;
  }
  return 0;
}

void runCase(size_t index, int phase) {
  gSkip.clear();
  gReported.clear();
  for (int attempt = 0; attempt < 40; ++attempt) {
    const int crashed = runCaseOnce(index, phase);
    // -1: died before announcing any step (setup); nothing to skip past.
    if (crashed <= 0) return;
    for (int k : gSkip)
      if (k == crashed) return; // not a skippable step (setup code)
    gSkip.push_back(crashed);
  }
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  const char *filter = nullptr;
  for (int i = 1; i < argc; ++i)
    if (argv[i][0] != '-') filter = argv[i];
  // Keep the children's "not initialized" warnings out of the way.
  lineBufferedStdout();
  const char *tmp = std::getenv("TMPDIR");
  gLogPath = std::string(tmp != nullptr ? tmp : "/tmp") +
             "/bindings_null_args_child_" + std::to_string(getpid()) + ".log";
  size_t ran = 0;
  for (size_t i = 0; i < cases().size(); ++i) {
    if (filter != nullptr && std::strstr(cases()[i].name, filter) == nullptr)
      continue;
    for (int phase = kPre; phase <= kPost; ++phase) runCase(i, phase);
    ++ran;
  }
  std::printf("%zu cases x 3 phases\n", ran);
  std::remove(gLogPath.c_str());
  return robustness::finish("bindings_null_args_test");
}

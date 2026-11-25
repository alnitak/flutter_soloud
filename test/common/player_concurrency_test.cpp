// Standalone concurrency test for Player (src/player.cpp) through the
// exports Dart calls (src/bindings.cpp), on miniaudio's null backend.
//
// The threads follow the plugin's thread model:
//
//   - loader threads (Dart runs loadMem / loadFile in `compute` isolates):
//     load the same few names and new ones, from memory and from a file,
//     into memory and as streams, while everything else runs, including the
//     engine teardown and re-initialization;
//   - API threads (Dart's UI isolate): play, stop, pause, seek, set volume,
//     query, stop a source, dispose sounds, on the same sounds the loaders
//     publish. Dart never calls these while a deinit is in progress (it
//     turns the engine "not initialized" first), so the lifecycle thread
//     waits for them to go idle around dispose() / initEngine();
//   - a realtime feeder (a background isolate; these exports hold a lease
//     on the Player and must survive a teardown at any moment);
//   - a samples reader (readSamplesFromMem, no engine);
//   - the lifecycle thread: requestEngineShutdown + dispose, then
//     prepareEngineInit + initEngine + setDartEventCallback, repeatedly, as
//     deinitAsync() / init() do;
//   - the audio thread of the null device, and the voice-ended / file-loaded
//     callbacks it and the loaders trigger.
//
// Checks: no crash, no hang (watchdog), nothing reported by ASan / TSan, the
// lifecycle calls succeed, and the engine is usable at the end (a loaded
// sound plays, stopAll() leaves no voice, disposeAllSound() leaves no sound).
//
//   ./test/common/player_concurrency_test.sh [--asan|--tsan] [--strict]
//       [--api-threads N] [--cycles N]

#include "bindings.h"
#include "enums.h"

#include "../support/robustness_checks.h"
#include "../support/temp_dir.h"

#ifdef _WIN32
#include <process.h>  // _getpid, _exit
#define getpid _getpid
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

std::vector<unsigned char> wavBytes(unsigned int frames) {
  const unsigned int rate = 44100;
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
    put16(44 + i * 2, static_cast<uint16_t>(static_cast<int16_t>(
                          6000 * std::sin(i * 0.05))));
  return b;
}

const std::vector<unsigned char> gShort = wavBytes(2205);  // 50 ms
const std::vector<unsigned char> gLong = wavBytes(22050);  // 500 ms
std::string gWavPath;

// ---- Dart callbacks (NativeCallable.listener: Dart frees the arguments) --
std::atomic<int> gVoiceEnded{0}, gFileLoaded{0}, gStateChanged{0};
void onVoiceEnded(unsigned int *handle) {
  ++gVoiceEnded;
  nativeFree(handle);
}
void onFileLoaded(enum PlayerErrors *error, char *name, unsigned int *hash,
                  uint64_t *counter) {
  ++gFileLoaded;
  nativeFree(error);
  nativeFree(name);
  nativeFree(hash);
  nativeFree(counter);
}
void onStateChanged(enum PlayerStateEvents *state) {
  ++gStateChanged;
  nativeFree(state);
}

bool startEngine() {
  prepareEngineInit(-1);
  const PlayerErrors e = initEngine(-1, 44100, 1024, 2, 0, 0, 0);
  if (e != noError) {
    std::printf("  initEngine failed: %d\n", e);
    return false;
  }
  setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, -1);
  return true;
}

void stopEngine() {
  requestEngineShutdown();
  dispose();
}

// ---- shared state ---------------------------------------------------------
std::atomic<bool> gRunning{true};
std::atomic<long> gProgress{0};

// Dart's "engine initialized" flag as the UI isolate sees it.
std::atomic<bool> gUiAllowed{false};

std::mutex gPoolMutex;
std::vector<unsigned int> gHashes;
std::vector<unsigned int> gHandles;

void publishHash(unsigned int h) {
  std::lock_guard<std::mutex> l(gPoolMutex);
  if (gHashes.size() > 64) gHashes.erase(gHashes.begin());
  gHashes.push_back(h);
}
unsigned int pickHash(std::mt19937 &rng) {
  std::lock_guard<std::mutex> l(gPoolMutex);
  if (gHashes.empty()) return 0;
  return gHashes[rng() % gHashes.size()];
}
void forgetHash(unsigned int h) {
  std::lock_guard<std::mutex> l(gPoolMutex);
  for (size_t i = 0; i < gHashes.size(); ++i)
    if (gHashes[i] == h) {
      gHashes.erase(gHashes.begin() + i);
      return;
    }
}
void publishHandle(unsigned int h) {
  std::lock_guard<std::mutex> l(gPoolMutex);
  if (gHandles.size() > 64) gHandles.erase(gHandles.begin());
  gHandles.push_back(h);
}
unsigned int pickHandle(std::mt19937 &rng) {
  std::lock_guard<std::mutex> l(gPoolMutex);
  if (gHandles.empty()) return 0;
  return gHandles[rng() % gHandles.size()];
}
void clearPools() {
  std::lock_guard<std::mutex> l(gPoolMutex);
  gHashes.clear();
  gHandles.clear();
}

std::atomic<int> gUnexpected{0};
// Codes a call may legitimately return while sounds come and go and the
// engine is torn down.
bool acceptable(PlayerErrors e) {
  switch (e) {
  case noError:
  case backendNotInited:
  case soundHashNotFound:
  case invalidParameter:
  case fileAlreadyLoaded:
  case maxNumberOfFiltersReached:
  case maxActiveVoiceCountReached: // many looping voices at once
    return true;
  default:
    return false;
  }
}
void expectAcceptable(const char *what, PlayerErrors e) {
  if (!acceptable(e)) {
    if (++gUnexpected <= 10) std::printf("  %s returned %d\n", what, e);
  }
}

// ---- threads --------------------------------------------------------------
void loaderThread(int id) {
  std::mt19937 rng(100 + id);
  long n = 0;
  while (gRunning.load()) {
    ++n;
    const bool shared = (rng() % 3) != 0;
    std::string name = shared ? "shared_" + std::to_string(rng() % 4)
                              : "loader" + std::to_string(id) + "_" +
                                    std::to_string(n);
    const auto &bytes = (rng() % 4 == 0) ? gLong : gShort;
    unsigned int hash = 0;
    if (rng() % 8 == 0 && !gWavPath.empty()) {
      // Reports through the file-loaded callback.
      loadFile(const_cast<char *>(gWavPath.c_str()), rng() % 2 == 0, n);
    } else {
      const PlayerErrors e =
          loadMem(const_cast<char *>(name.c_str()),
                  const_cast<unsigned char *>(bytes.data()),
                  static_cast<int>(bytes.size()), rng() % 2, &hash);
      expectAcceptable("loadMem", e);
      if ((e == noError || e == fileAlreadyLoaded) && hash != 0)
        publishHash(hash);
    }
    ++gProgress;
    if (rng() % 4 == 0) std::this_thread::sleep_for(1ms);
  }
}

std::atomic<int> gUiBusy{0};

void apiThread(int id) {
  std::mt19937 rng(200 + id);
  while (gRunning.load()) {
    if (!gUiAllowed.load()) {
      std::this_thread::sleep_for(200us);
      continue;
    }
    ++gUiBusy;
    // Re-check under the busy count, so the lifecycle thread's wait for
    // idle covers this call.
    if (!gUiAllowed.load()) {
      --gUiBusy;
      continue;
    }
    const unsigned int hash = pickHash(rng);
    const unsigned int handle = pickHandle(rng);
    switch (rng() % 14) {
    case 0:
    case 1:
    case 2: {
      unsigned int h = 0;
      const PlayerErrors e = play(hash, 0, 0.5f, 0.0f, rng() % 4 == 0,
                                  rng() % 3 == 0, 0.0, 0.0, -1, -1, 1.0f, &h);
      expectAcceptable("play", e);
      if (e == noError && h != 0) publishHandle(h);
      break;
    }
    case 3:
      stop(handle);
      break;
    case 4:
      pauseSwitch(handle);
      break;
    case 5:
      setVolume(handle, 0.3f);
      fadeVolume(handle, 0.0f, 0.05f);
      break;
    case 6:
      seek(handle, 0.01f);
      getPosition(handle);
      break;
    case 7:
      if (rng() % 3 == 0) {
        disposeSound(hash);
        forgetHash(hash);
      }
      break;
    case 8:
      stopAudioSource(hash);
      break;
    case 9:
      countAudioSource(hash);
      getLength(hash);
      getActiveVoiceCount();
      break;
    case 10:
      getIsValidVoiceHandle(handle);
      getVolume(handle);
      getPause(handle);
      break;
    case 11:
      setLooping(handle, rng() % 2 == 0);
      scheduleStop(handle, 0.02f);
      break;
    case 12: {
      unsigned int h = 0;
      expectAcceptable("play3d",
                       play3d(hash, 0, 1.0f, 0, 0, 0, 0, 0, 0.5f, false,
                              false, 0.0, &h));
      if (h != 0) publishHandle(h);
      break;
    }
    case 13:
      if (rng() % 20 == 0) stopAll();
      break;
    }
    --gUiBusy;
    ++gProgress;
  }
}

void feederThread() {
  std::vector<float> chunk(256 * 2, 0.1f);
  unsigned int hash = 0;
  while (gRunning.load()) {
    if (hash == 0) {
      const PlayerErrors e = setRealtimeStream(&hash, 44100, 2, 0, 4096, 1024);
      if (e != noError) {
        hash = 0;
        std::this_thread::sleep_for(1ms);
        continue;
      }
    }
    // Immediate pushes take the lock-free path; clocked and scheduled ones
    // take the audio mutex (and so dispatch the voices that ended meanwhile
    // on this thread, as a feeder isolate does).
    static unsigned int n = 0;
    const unsigned int count = static_cast<unsigned int>(chunk.size());
    unsigned int pushed = 0;
    switch (++n % 3) {
    case 0:
      pushed = pushRealtimeFloat32(hash, chunk.data(), 256, count);
      break;
    case 1:
      pushed = pushRealtimeClockedFloat32(hash, n * 0.005, chunk.data(), 256,
                                          count);
      break;
    default:
      // Only the leased realtime exports are called here (getEngineTime()
      // is a UI-isolate call), so the time is a rough local clock.
      pushed = pushRealtimeScheduledFloat32(hash, n * 0.004, chunk.data(),
                                            256, count);
    }
    if (n % 50 == 0) resetRealtimeStream(hash);
    getRealtimeAvailableToRead(hash);
    getRealtimeUnderrunCount(hash);
    if (pushed == 0) {
      // Full, or the stream died with the engine: start a new one.
      if (getRealtimeAvailableToWrite(hash) == 0 && isInited() == 0)
        hash = 0;
      std::this_thread::sleep_for(500us);
    }
    ++gProgress;
  }
}

void readerThread() {
  std::vector<float> samples(256);
  while (gRunning.load()) {
    readSamplesFromMem(gShort.data(), gShort.size(), 0.0f, 0.04f, 256, true,
                       samples.data());
    ++gProgress;
    std::this_thread::sleep_for(1ms);
  }
}

void watchdog() {
  long last = -1;
  int stalls = 0;
  while (gRunning.load()) {
    std::this_thread::sleep_for(1s);
    const long now = gProgress.load();
    if (now == last) {
      if (++stalls >= 60) {
        std::printf("  FAIL: no progress for 60 s (deadlock?)\n");
        std::fflush(stdout);
        _exit(3);
      }
    } else {
      stalls = 0;
    }
    last = now;
  }
}

} // namespace

int main(int argc, char **argv) {
  robustness::parseArgs(argc, argv);
  int apiThreads = 1;
  int cycles = 12;
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], "--api-threads") == 0)
      apiThreads = std::atoi(argv[i + 1]);
    if (std::strcmp(argv[i], "--cycles") == 0) cycles = std::atoi(argv[i + 1]);
  }
  gWavPath = testTempPath("player_concurrency_" + std::to_string(getpid()) + ".wav");
  {
    std::ofstream f(gWavPath, std::ios::binary);
    f.write(reinterpret_cast<const char *>(gShort.data()), gShort.size());
  }

  std::printf("player concurrency: %d API thread(s), %d deinit/init cycles\n",
              apiThreads, cycles);
  if (!startEngine()) {
    std::printf("SKIPPED: the engine does not initialize on the null "
                "backend\n");
    std::remove(gWavPath.c_str());
    return 0;
  }
  gUiAllowed.store(true);

  std::vector<std::thread> threads;
  threads.emplace_back(watchdog);
  for (int i = 0; i < 2; ++i) threads.emplace_back(loaderThread, i);
  for (int i = 0; i < apiThreads; ++i) threads.emplace_back(apiThread, i);
  threads.emplace_back(feederThread);
  threads.emplace_back(readerThread);

  int initFailures = 0;
  for (int c = 0; c < cycles; ++c) {
    std::this_thread::sleep_for(150ms);
    // Dart: _predeinit() turns the engine "not initialized" for the UI
    // isolate, then the native dispose runs (in Isolate.run for
    // deinitAsync). Loaders, the feeder and the reader keep going.
    gUiAllowed.store(false);
    while (gUiBusy.load() != 0) std::this_thread::yield();
    stopEngine();
    ++gProgress;
    clearPools();
    std::this_thread::sleep_for(20ms);
    if (!startEngine()) ++initFailures;
    ++gProgress;
    gUiAllowed.store(true);
  }
  std::this_thread::sleep_for(150ms);
  gUiAllowed.store(false);
  while (gUiBusy.load() != 0) std::this_thread::yield();
  gRunning.store(false);
  for (auto &t : threads) t.join();

  // Focused phase: the UI thread pauses / resumes voices of short sounds
  // while a feeder's clocked pushes take the audio mutex, so the voices that
  // end in the mix are dispatched (Player::removeHandle) on the feeder
  // thread as often as on the UI thread.
  {
    std::printf("  focused: pauseSwitch / play vs voices ending on the audio and feeder "
                "threads\n");
    unsigned int shortHash = 0;
    loadMem(const_cast<char *>("pause_race"),
            const_cast<unsigned char *>(gShort.data()),
            static_cast<int>(gShort.size()), 1, &shortHash);
    std::atomic<bool> phaseRunning{true};
    gRunning.store(true);
    std::thread feeder(feederThread);
    std::thread ui([&] {
      std::vector<unsigned int> handles;
      while (phaseRunning.load()) {
        unsigned int h = 0;
        if (play(shortHash, 0, 0.2f, 0.0f, false, false, 0.0, 0.0, -1, -1,
                 4.0f, &h) == noError)
          handles.push_back(h);
        if (handles.size() > 24) handles.erase(handles.begin());
        for (unsigned int x : handles) {
          pauseSwitch(x);
          pauseSwitch(x);
          setPause(x, false);
        }
        ++gProgress;
      }
    });
    std::this_thread::sleep_for(3s);
    phaseRunning.store(false);
    ui.join();
    gRunning.store(false);
    feeder.join();
    stopAll();
    disposeSound(shortHash);
  }

  CHECK(initFailures == 0, "%d re-initializations failed", initFailures);
  CHECK(gUnexpected == 0, "%d calls returned an unexpected error",
        gUnexpected.load());

  // The engine is still usable.
  CHECK(isInited() == 1, "engine initialized at the end");
  unsigned int hash = 0;
  PlayerErrors e = loadMem(const_cast<char *>("final"),
                           const_cast<unsigned char *>(gLong.data()),
                           static_cast<int>(gLong.size()), 1, &hash);
  CHECK(e == noError && hash != 0, "final load (%d)", e);
  unsigned int handle = 0;
  e = play(hash, 0, 1.0f, 0.0f, false, true, 0.0, 0.0, -1, -1, 1.0f, &handle);
  CHECK(e == noError && getIsValidVoiceHandle(handle) == 1, "final play (%d)",
        e);
  std::this_thread::sleep_for(50ms);
  CHECK(getIsValidVoiceHandle(handle) == 1, "the looping voice keeps playing");
  stopAll();
  std::this_thread::sleep_for(50ms);
  CHECK(getActiveVoiceCount() == 0, "stopAll leaves no voice (%u)",
        getActiveVoiceCount());
  disposeAllSound();
  CHECK(countAudioSource(hash) == 0, "disposeAllSound removed the sound");
  e = play(hash, 0, 1.0f, 0.0f, false, false, 0.0, 0.0, -1, -1, 1.0f,
           &handle);
  CHECK(e == soundHashNotFound, "a disposed sound does not play (%d)", e);
  std::printf("  callbacks: %d voice ended, %d file loaded, %d state\n",
              gVoiceEnded.load(), gFileLoaded.load(), gStateChanged.load());
  stopEngine();
  CHECK(isInited() == 0, "engine down after dispose");
  std::remove(gWavPath.c_str());
  return robustness::finish("player_concurrency_test");
}

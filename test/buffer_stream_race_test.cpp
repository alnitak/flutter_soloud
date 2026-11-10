// The threads that share a BufferStream:
//
//   * a feeder delivering data (addAudioDataStream()), under sounds_mutex;
//   * the stream's background decode thread, which decodes what a compressed
//     stream still holds once its data has ended (setDataIsEnded()) and then
//     checks the buffering: it unpauses the voices that can play again;
//   * the mixer (getAudio(), with SoLoud's audio mutex held), which pauses a
//     voice that has run out of data and reports it;
//   * control threads that play, stop, reset and read the stream.
//
// Scenarios (run one by naming it):
//
//   decode-reset  The decode thread parks right before a buffering check that
//                 will unpause a voice, and the stream is reset meanwhile: the
//                 reset holds sounds_mutex while it stops the thread and waits
//                 for it. Released, the thread must stop instead of waiting
//                 for that mutex, so the reset finishes.
//   group-unpause A voice paused by the user and unpaused through a voice
//                 group runs dry, and the mixer pauses it for buffering. The
//                 next delivery must resume it; a pause by the user while it
//                 buffers must still hold it.
//   stress        For 1.5 s each: a PRESERVED stream of raw PCM is fed in
//                 short bursts, so its voice keeps running dry and the mixer
//                 pauses it, while another thread plays and stops more voices
//                 of it and another reads its length and size; a RELEASED one
//                 is fed while it plays and its length and consumed time are
//                 read; and an MP3 stream is fed while a voice of it already
//                 mixes, so its first decode sets the format the mixer reads.
//                 Every buffering event must name a voice of the stream, and
//                 under ThreadSanitizer nothing may race.
//
// A scenario that deadlocks cannot be cleaned up after: the run then prints
// its counts and exits with failure.
//
// The streams are driven through the exports Dart uses, on a real engine (the
// null backend). Barriers (device_lifecycle_test_hooks.h) park the threads at
// the points that matter.
//
//   ./test/run_buffer_stream_race_test.sh [--asan|--tsan] [scenario]
#include "bindings.h"
#include "device_lifecycle_test_hooks.h"
#include "enums.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
using soloud_test::DeviceBarrier;

constexpr int64_t kEngineId = 4022;
constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kChannels = 2;
// setBufferStream() takes these as plain ints.
constexpr int kPreserved = 0;
constexpr int kReleased = 1;
constexpr int kPcmF32 = 0;
constexpr int kAuto = 4;

int gAssertions = 0;
int gFailures = 0;
#define EXPECT(cond, ...)                                                      \
    do {                                                                       \
        ++gAssertions;                                                         \
        if (!(cond)) {                                                         \
            ++gFailures;                                                       \
            std::printf("  FAIL: ");                                           \
            std::printf(__VA_ARGS__);                                          \
            std::printf("\n");                                                 \
        }                                                                      \
    } while (0)

struct BufferingEvent {
    bool isBuffering;
    unsigned int handle;
};
std::mutex gBufferingMutex;
std::vector<BufferingEvent> gBuffering;

void onBuffering(bool isBuffering, unsigned int handle, double time) {
    (void)time;
    std::lock_guard<std::mutex> lock(gBufferingMutex);
    gBuffering.push_back({isBuffering, handle});
}

void clearBufferingEvents() {
    std::lock_guard<std::mutex> lock(gBufferingMutex);
    gBuffering.clear();
}

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                  unsigned long long *t) {
    (void)e; (void)n; (void)h; (void)t;
}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

std::vector<unsigned char> readFile(const char *path, size_t limit) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (data.size() > limit) data.resize(limit);
    return data;
}

/// Runs [fn] on its own thread and waits up to [ms] for it to return. A
/// thread that does not return is left running: the caller then reports a
/// deadlock and ends the run.
bool returnsWithin(int ms, std::function<void()> fn) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::thread([fn, done] {
        fn();
        done->store(true);
    }).detach();
    for (int i = 0; i < ms / 5 && !done->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return done->load();
}

bool barrierReached(DeviceBarrier barrier, int ms) {
    return returnsWithin(ms, [barrier] { soloud_test::waitBarrierReached(barrier); });
}

[[noreturn]] void deadlocked(const char *what) {
    EXPECT(false, "%s", what);
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    std::fflush(stdout);
    _exit(1);
}

unsigned int playVoice(unsigned int hash) {
    unsigned int voice = 0;
    const PlayerErrors err = play(hash, 0, 0.05f, 0.0f, false, false, 0.0, 0.0,
                                  -1, -1, 1.0f, &voice);
    return err == PlayerErrors::noError ? voice : 0;
}

size_t bufferingStarts(unsigned int voice) {
    std::lock_guard<std::mutex> lock(gBufferingMutex);
    return std::count_if(gBuffering.begin(), gBuffering.end(),
                         [voice](const BufferingEvent &e) {
                             return e.isBuffering && e.handle == voice;
                         });
}

/// Waits up to [ms] for the mixer to pause [voice] for buffering more than
/// [before] times.
bool ranDry(unsigned int voice, size_t before, int ms) {
    for (int i = 0; i < ms / 10 && bufferingStarts(voice) <= before; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return bufferingStarts(voice) > before;
}

void deliverSeconds(unsigned int hash, double seconds) {
    const std::vector<float> pcm(
        static_cast<size_t>(seconds * kSampleRate) * kChannels, 0.01f);
    const PlayerErrors err = addAudioDataStream(
        hash, reinterpret_cast<const unsigned char *>(pcm.data()),
        static_cast<unsigned int>(pcm.size() * sizeof(float)));
    EXPECT(err == PlayerErrors::noError, "addAudioDataStream: %d", (int)err);
}

/// setPause() on a voice group pauses or unpauses its voices, but finds no
/// voice of a sound for the group's handle, so a voice the user paused and
/// then unpaused through its group keeps its user-paused flag. Once the
/// mixer has paused it for running out of data, the next delivery must
/// resume it anyway. A pause by the user after the mixer's still holds.
void groupUnpause() {
    std::printf("group-unpause: a voice unpaused through its group runs dry\n");
    clearBufferingEvents();
    unsigned int hash = 0;
    PlayerErrors err = setBufferStream(&hash, 64u << 20, kPreserved,
                                       /*bufferingTimeNeeds*/ 0.5, kSampleRate,
                                       kChannels, kPcmF32, onBuffering, nullptr);
    EXPECT(err == PlayerErrors::noError, "setBufferStream: %d", (int)err);
    deliverSeconds(hash, 1.0);
    const unsigned int voice = playVoice(hash);
    EXPECT(voice != 0, "the stream should play");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    setPause(voice, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const unsigned int group = createVoiceGroup();
    addVoiceToGroup(group, voice);
    setPause(group, false);
    EXPECT(getPause(voice) == 0, "the group should unpause the voice");

    EXPECT(ranDry(voice, 0, 3000), "the voice should run dry");
    deliverSeconds(hash, 0.6);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(getPause(voice) == 0,
           "the delivery should resume the voice the mixer paused");
    EXPECT(getPosition(voice) > 1.05, "the voice should play on, at %.2f",
           getPosition(voice));

    // Paused by the user while it buffers: the user's pause holds.
    EXPECT(ranDry(voice, 1, 3000), "the voice should run dry again");
    setPause(voice, true);
    deliverSeconds(hash, 1.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(getPause(voice) != 0,
           "a delivery should not resume a voice the user paused");
    setPause(voice, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT(getPause(voice) == 0, "the user should resume the voice");

    destroyVoiceGroup(group);
    stop(voice);
    disposeSound(hash);
}

/// The decode thread parks right before a buffering check that will unpause
/// a voice, while a reset of the stream holds sounds_mutex and waits for the
/// thread to stop. The check pauses and unpauses voices through the Player,
/// which takes sounds_mutex: had the thread gone on to wait for it, neither
/// could finish.
void decodeThreadAgainstReset() {
    std::printf("decode-reset: the decode thread's buffering check while a "
                "reset waits for it\n");
    // Its first 4 KiB decode to 25438 samples (44.1 kHz stereo): more than
    // the 16384 setDataIsEnded() decodes itself, so the decode thread gets
    // the rest.
    const std::vector<unsigned char> mp3 =
        readFile("example/assets/audio/explosion.mp3", 4096);
    EXPECT(mp3.size() == 4096, "the MP3 asset should be readable from the "
                               "repository root (read %zu bytes)", mp3.size());
    if (mp3.size() != 4096) return;

    unsigned int hash = 0;
    PlayerErrors err = setBufferStream(&hash, 10 * 1024 * 1024, kPreserved,
                                       /*bufferingTimeNeeds*/ 10.0, kSampleRate,
                                       kChannels, kAuto, onBuffering, nullptr);
    EXPECT(err == PlayerErrors::noError, "setBufferStream: %d", (int)err);
    // Kept undecoded: the stream decodes once it holds more than 4 KiB.
    err = addAudioDataStream(hash, mp3.data(),
                             static_cast<unsigned int>(mp3.size()));
    EXPECT(err == PlayerErrors::noError, "addAudioDataStream: %d", (int)err);
    const unsigned int voice = playVoice(hash);
    EXPECT(voice != 0, "the stream should play");
    EXPECT(getPause(voice) != 0,
           "with nothing decoded, buffering should pause the voice");
    // Slow, so the first chunk outlasts the scenario once the voice plays.
    setRelativePlaySpeed(voice, 0.05f);

    soloud_test::armBarrier(DeviceBarrier::bufferStreamBackgroundDecoded);
    err = setDataIsEnded(hash);
    EXPECT(err == PlayerErrors::noError, "setDataIsEnded: %d", (int)err);
    if (!barrierReached(DeviceBarrier::bufferStreamBackgroundDecoded, 3000))
        deadlocked("the stream's decode thread never decoded a chunk; the "
                   "scenario needs it to");

    // setDataIsEnded() unpaused the voice. Pause it through the engine, which
    // leaves its user-paused flag clear, so the decode thread's check will
    // unpause it again.
    schedulePause(voice, 0.0f);
    EXPECT(getPause(voice) != 0, "schedulePause(0) should pause the voice");

    // The reset holds sounds_mutex while it stops and joins the decode thread.
    auto reset = std::make_shared<std::atomic<bool>>(false);
    std::thread([hash, reset] {
        resetBufferStream(hash);
        reset->store(true);
    }).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(!reset->load(), "the reset should wait for the parked decode thread");

    soloud_test::releaseBarrier(DeviceBarrier::bufferStreamBackgroundDecoded);
    for (int i = 0; i < 400 && !reset->load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!reset->load())
        deadlocked("the reset never finished: the decode thread's buffering "
                   "check waits for the sounds_mutex that the reset holds "
                   "while it waits for the thread");
    EXPECT(getPause(voice) != 0,
           "the stopped decode thread should not have unpaused the voice");
    stop(voice);
    disposeSound(hash);
}

/// Whether every buffering event names one of [voices].
int strangers(const std::vector<unsigned int> &voices) {
    std::lock_guard<std::mutex> lock(gBufferingMutex);
    int count = 0;
    for (const BufferingEvent &e : gBuffering)
        if (std::find(voices.begin(), voices.end(), e.handle) == voices.end())
            ++count;
    return count;
}

int pausesReported() {
    std::lock_guard<std::mutex> lock(gBufferingMutex);
    int count = 0;
    for (const BufferingEvent &e : gBuffering) count += e.isBuffering;
    return count;
}

/// A PRESERVED stream of raw PCM fed 30 ms every 40 ms: its voice keeps
/// running dry and the mixer pauses it, while another thread plays and stops
/// more voices of it (adding to and removing from the sound's handle list)
/// and another reads its length and size.
void stressPreserved() {
    std::printf("stress: a PRESERVED stream's voices run dry while voices of "
                "it are played and stopped\n");
    unsigned int hash = 0;
    PlayerErrors err = setBufferStream(&hash, 32 * 1024 * 1024, kPreserved,
                                       /*bufferingTimeNeeds*/ 0.02, kSampleRate,
                                       kChannels, kPcmF32, onBuffering, nullptr);
    EXPECT(err == PlayerErrors::noError, "setBufferStream: %d", (int)err);
    const std::vector<float> burst(kSampleRate * kChannels * 30 / 1000, 0.001f);
    const auto feed = [&] {
        return addAudioDataStream(
            hash, reinterpret_cast<const unsigned char *>(burst.data()),
            static_cast<unsigned int>(burst.size() * sizeof(float)));
    };
    EXPECT(feed() == PlayerErrors::noError, "the first delivery");
    std::mutex playedMutex;
    std::vector<unsigned int> played;
    const unsigned int first = playVoice(hash);
    EXPECT(first != 0, "the stream should play");
    played.push_back(first);
    clearBufferingEvents();

    std::atomic<bool> running{true};
    std::atomic<int> feedErrors{0};
    std::thread feeder([&] {
        while (running.load()) {
            if (feed() != PlayerErrors::noError) ++feedErrors;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
    std::thread player([&] {
        while (running.load()) {
            const unsigned int voice = playVoice(hash);
            if (voice != 0) {
                {
                    std::lock_guard<std::mutex> lock(playedMutex);
                    played.push_back(voice);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
                stop(voice);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::thread reader([&] {
        while (running.load()) {
            unsigned int size = 0;
            (void)getLength(hash);
            (void)getBufferSize(hash, &size);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    running.store(false);
    feeder.join();
    player.join();
    reader.join();

    EXPECT(feedErrors.load() == 0, "%d deliveries failed", feedErrors.load());
    EXPECT(pausesReported() > 0, "the voice should have been paused for "
                                 "buffering");
    {
        std::lock_guard<std::mutex> lock(playedMutex);
        const int unknown = strangers(played);
        EXPECT(unknown == 0, "%d buffering event(s) named a voice the stream "
                             "never played", unknown);
    }
    stop(first);
    disposeSound(hash);
}

/// A RELEASED stream of raw PCM fed 30 ms every 40 ms while its voice plays:
/// the mixer counts what it consumes as deliveries count what they add, and
/// another thread reads the length and the consumed time.
void stressReleased() {
    std::printf("stress: a RELEASED stream is fed, played and read at once\n");
    unsigned int hash = 0;
    PlayerErrors err = setBufferStream(&hash, 32 * 1024 * 1024, kReleased,
                                       /*bufferingTimeNeeds*/ 0.02, kSampleRate,
                                       kChannels, kPcmF32, onBuffering, nullptr);
    EXPECT(err == PlayerErrors::noError, "setBufferStream: %d", (int)err);
    const std::vector<float> burst(kSampleRate * kChannels * 30 / 1000, 0.001f);
    const auto feed = [&] {
        return addAudioDataStream(
            hash, reinterpret_cast<const unsigned char *>(burst.data()),
            static_cast<unsigned int>(burst.size() * sizeof(float)));
    };
    EXPECT(feed() == PlayerErrors::noError, "the first delivery");
    const unsigned int voice = playVoice(hash);
    EXPECT(voice != 0, "the stream should play");

    std::atomic<bool> running{true};
    std::atomic<int> feedErrors{0};
    std::thread feeder([&] {
        while (running.load()) {
            if (feed() != PlayerErrors::noError) ++feedErrors;
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    });
    std::thread reader([&] {
        while (running.load()) {
            float consumed = 0.0f;
            (void)getLength(hash);
            (void)getStreamTimeConsumed(hash, &consumed);
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    running.store(false);
    feeder.join();
    reader.join();

    EXPECT(feedErrors.load() == 0, "%d deliveries failed", feedErrors.load());
    float consumed = 0.0f;
    EXPECT(getStreamTimeConsumed(hash, &consumed) == PlayerErrors::noError &&
               consumed > 0.5f,
           "the voice should have consumed most of 1.5 s, consumed %.3f s",
           consumed);
    stop(voice);
    disposeSound(hash);
}

/// An MP3 stream whose voice mixes before any data has arrived: its first
/// decode then sets the format the mixer reads.
void stressFirstDecode() {
    std::printf("stress: an MP3 stream's first decode while its voice mixes\n");
    const std::vector<unsigned char> mp3 =
        readFile("example/assets/audio/explosion.mp3", 256 * 1024);
    EXPECT(mp3.size() > 16 * 1024, "the MP3 asset should be readable from the "
                                   "repository root");
    unsigned int hash = 0;
    PlayerErrors err = setBufferStream(&hash, 32 * 1024 * 1024, kPreserved,
                                       /*bufferingTimeNeeds*/ 0.0, kSampleRate,
                                       kChannels, kAuto, onBuffering, nullptr);
    EXPECT(err == PlayerErrors::noError, "setBufferStream: %d", (int)err);
    const unsigned int voice = playVoice(hash);
    EXPECT(voice != 0, "the stream should play");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (size_t offset = 0; offset < mp3.size(); offset += 8192) {
        const size_t len = std::min<size_t>(8192, mp3.size() - offset);
        err = addAudioDataStream(hash, mp3.data() + offset,
                                 static_cast<unsigned int>(len));
        EXPECT(err == PlayerErrors::noError, "delivery at %zu: %d", offset,
               (int)err);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(getLength(hash) > 0.0, "the stream should hold decoded audio");
    stop(voice);
    disposeSound(hash);
}

void stress() {
    stressPreserved();
    stressReleased();
    stressFirstDecode();
}
} // namespace

int main(int argc, char **argv) {
    const std::string only = argc > 1 ? argv[1] : "";
    const auto want = [&](const char *name) { return only.empty() || only == name; };

    setAudioDeviceIdleTimeout(600000);
    prepareEngineInit(kEngineId);
    if (initEngine(-1, kSampleRate, 2048, kChannels, 1, 0, 0) !=
        PlayerErrors::noError) {
        std::printf("engine did not start\n");
        return 2;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged, kEngineId);

    if (want("decode-reset")) decodeThreadAgainstReset();
    if (want("group-unpause")) groupUnpause();
    if (want("stress")) stress();

    dispose();
    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

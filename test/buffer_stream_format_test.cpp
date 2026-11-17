// An AUTO buffer stream's voice adopts the format the stream detects in its
// data. These check that it does so again for a new source delivered after a
// reset, and that adopting a format keeps the voice's relative play speed.
//
// Scenarios (run one by naming it):
//
//   reset             A voice keeps playing while its stream is reset and
//                     refilled with a 22.05 kHz source in place of a 44.1 kHz
//                     one: its base rate must become 22050 and a 1 kHz tone
//                     keep its pitch. Reset again at 2x, it must stay at 2x.
//   play-before-data  The voice plays and is set to 2x before any data has
//                     arrived: once the first chunk's format is adopted, it
//                     must still play at 2x.
//   speed-seek        A voice at 2x keeps its speed across a seek to the
//                     start, which must not re-read the format.
//   reset-mixing      A thread mixes the voice block after block while the
//                     stream is reset 50 times and refilled at alternating
//                     rates. A reset clears the detected format the mixer
//                     reads: under ThreadSanitizer nothing may race, and the
//                     voice must end on the last source's rate.
//
// Each scenario starts its own engine and mixes it by hand, with the device
// stopped and render-ahead off, so the test is the only mixer. The pitch is
// measured by the tone's positive zero crossings in 5120 engine frames: about
// 116 at normal speed, about 233 at double speed.
//
//   ./test/run_buffer_stream_format_test.sh [--asan|--tsan] [scenario]
#include "bindings.h"
#include "enums.h"
#include "player.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// The plugin's global player, from bindings.cpp.
extern "C" std::unique_ptr<Player> &player;

namespace {
// setBufferStream() takes these as plain ints.
constexpr int kPreserved = 0;
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

int64_t gEngineId = 4023;

void onVoiceEnded(unsigned int *h) { std::free(h); }
void onFileLoaded(enum PlayerErrors *e, char *n, unsigned int *h,
                  unsigned long long *t) {
    (void)e; (void)n; (void)h; (void)t;
}
void onStateChanged(enum PlayerStateEvents *s) { std::free(s); }

/// A mono 32-bit float WAV file holding [frames] frames of a 1 kHz tone.
std::vector<unsigned char> toneWav(unsigned int rate, unsigned int frames) {
    std::vector<unsigned char> data(44 + frames * 4);
    auto put = [&](size_t offset, unsigned int value, int count) {
        for (int i = 0; i < count; ++i) data[offset + i] = (value >> (8 * i)) & 255;
    };
    std::memcpy(data.data(), "RIFF", 4); put(4, data.size() - 8, 4);
    std::memcpy(data.data() + 8, "WAVEfmt ", 8); put(16, 16, 4);
    put(20, 3, 2); put(22, 1, 2); put(24, rate, 4); put(28, rate * 4, 4);
    put(32, 4, 2); put(34, 32, 2);
    std::memcpy(data.data() + 36, "data", 4); put(40, frames * 4, 4);
    for (unsigned int i = 0; i < frames; ++i) {
        const float sample =
            0.25f * std::sin(2.0 * 3.141592653589793 * 1000.0 * i / rate);
        std::memcpy(data.data() + 44 + i * 4, &sample, 4);
    }
    return data;
}

/// Two seconds of the tone at [rate].
std::vector<unsigned char> twoSeconds(unsigned int rate) {
    return toneWav(rate, rate * 2);
}

/// A play() and an unpause (a delivery's buffering check unpauses a voice)
/// post a device start, which the scheduler thread runs later. Stopping the
/// device again cancels a start still pending, or waits for one under way:
/// the test is then the only mixer until its next call into the plugin.
void keepTheDeviceStopped() {
    EXPECT(stopAudioDevice(1) == PlayerErrors::noError &&
               getAudioDeviceState() == audioDeviceStopped,
           "the device should stop for the test to mix alone");
}

void mixFrames(float *output, unsigned int frames) {
    player->soloud.mix(output, frames);
}

void mixBlocks(int blocks) {
    keepTheDeviceStopped();
    float output[512 * 2] = {};
    for (int i = 0; i < blocks; ++i) mixFrames(output, 512);
}

float voiceBaseRate(unsigned int handle) {
    auto &engine = player->soloud;
    engine.lockAudioMutex_internal();
    const int index = engine.getVoiceFromHandle_internal(handle);
    const float rate = index < 0 ? 0 : engine.mVoice[index]->mBaseSamplerate;
    engine.unlockAudioMutex_internal();
    return rate;
}

/// Positive zero crossings of the left channel in 5120 engine frames.
int crossings() {
    keepTheDeviceStopped();
    float output[512 * 2] = {};
    int count = 0;
    float previous = 0;
    for (int block = 0; block < 10; ++block) {
        mixFrames(output, 512);
        for (int i = 0; i < 512; ++i) {
            const float sample = output[2 * i];
            if (previous <= 0 && sample > 0) ++count;
            previous = sample;
        }
    }
    return count;
}

bool normalSpeed(int count) { return count >= 112 && count <= 120; }
bool doubleSpeed(int count) { return count >= 224 && count <= 240; }

/// Starts an engine the test mixes by hand, runs [scenario], disposes it.
template <typename Scenario> void withEngine(Scenario scenario) {
    prepareEngineInit(gEngineId++);
    // The last argument is the render-ahead depth: 0 keeps the direct mixing
    // path that mix() drives.
    if (initEngine(-1, 44100, 2048, 2, 1, 0, 0) != PlayerErrors::noError) {
        EXPECT(false, "engine init");
        return;
    }
    setDartEventCallback(onVoiceEnded, onFileLoaded, onStateChanged,
                         gEngineId - 1);
    scenario();
    dispose();
}

unsigned int autoStream() {
    unsigned int hash = 0;
    // Set up for 44.1 kHz mono; the data's own header decides.
    EXPECT(setBufferStream(&hash, 1024 * 1024, kPreserved,
                           /*bufferingTimeNeeds*/ 0.0, 44100, 1, kAuto,
                           nullptr, nullptr) == PlayerErrors::noError,
           "AUTO stream setup");
    return hash;
}

unsigned int playVoice(unsigned int hash) {
    unsigned int voice = 0;
    EXPECT(play(hash, 0, 0.2f, 0, false, false, 0, 0, -1, -1, 1, &voice) ==
                   PlayerErrors::noError &&
               voice != 0,
           "play");
    return voice;
}

void deliver(unsigned int hash, const std::vector<unsigned char> &data,
             const char *what) {
    EXPECT(addAudioDataStream(hash, data.data(),
                              static_cast<unsigned int>(data.size())) ==
               PlayerErrors::noError,
           "%s", what);
}

void resetKeepsUpWithTheSource() {
    std::printf("reset: a voice adopts a new source's rate after a reset\n");
    const unsigned int hash = autoStream();
    deliver(hash, twoSeconds(44100), "first source delivery");
    const unsigned int voice = playVoice(hash);
    mixBlocks(24);
    EXPECT(voiceBaseRate(voice) == 44100, "initial voice rate: %.0f",
           voiceBaseRate(voice));

    EXPECT(resetBufferStream(hash) == PlayerErrors::noError,
           "reset while the voice keeps playing");
    deliver(hash, twoSeconds(22050), "replacement source delivery");
    mixBlocks(24);
    const float rate = voiceBaseRate(voice);
    EXPECT(rate == 22050,
           "the voice should adopt the replacement's rate, got %.0f", rate);
    const int count = crossings();
    EXPECT(normalSpeed(count),
           "the replacement tone should keep its pitch: %d crossings (1x is "
           "about 116)", count);

    // Replacing the source must keep a speed already set on the voice too.
    setRelativePlaySpeed(voice, 2.0f);
    EXPECT(resetBufferStream(hash) == PlayerErrors::noError,
           "reset at 2x speed");
    deliver(hash, twoSeconds(44100), "replacement at the original rate");
    mixBlocks(24);
    EXPECT(voiceBaseRate(voice) == 44100,
           "the second replacement's rate, got %.0f", voiceBaseRate(voice));
    const int fast = crossings();
    EXPECT(doubleSpeed(fast),
           "the reset should keep the 2x speed: %d crossings (2x is about "
           "233)", fast);
    stop(voice);
    disposeSound(hash);
}

void speedSetBeforeTheFirstChunk() {
    std::printf("play-before-data: 2x set before the first chunk\n");
    const unsigned int hash = autoStream();
    const unsigned int voice = playVoice(hash);
    setRelativePlaySpeed(voice, 2.0f);
    deliver(hash, twoSeconds(44100), "delivery");
    const int count = crossings();
    EXPECT(doubleSpeed(count),
           "2x set before the first chunk should survive the format's "
           "adoption: %d crossings (2x is about 233, 1x about 116)", count);
    stop(voice);
    disposeSound(hash);
}

void speedKeptAcrossASeek() {
    std::printf("speed-seek: 2x kept across a seek to the start\n");
    const unsigned int hash = autoStream();
    deliver(hash, twoSeconds(44100), "delivery");
    const unsigned int voice = playVoice(hash);
    setRelativePlaySpeed(voice, 2.0f);
    const int before = crossings();
    EXPECT(doubleSpeed(before), "2x after play: %d crossings", before);
    EXPECT(seek(voice, 0.0f) == PlayerErrors::noError, "seek to the start");
    const int after = crossings();
    EXPECT(doubleSpeed(after), "2x after the seek: %d crossings", after);
    stop(voice);
    disposeSound(hash);
}

void resetWhileAnotherThreadMixes() {
    std::printf("reset-mixing: resets while another thread mixes the voice\n");
    const std::vector<unsigned char> fast = twoSeconds(44100);
    const std::vector<unsigned char> slow = twoSeconds(22050);
    const unsigned int hash = autoStream();
    deliver(hash, fast, "first source delivery");
    const unsigned int voice = playVoice(hash);
    keepTheDeviceStopped();

    // 2048 frames a block: the voice's data is read in four parts, so a
    // reset landing in a block is followed by reads in the same block.
    std::atomic<bool> mixing{true};
    std::thread mixer([&mixing] {
        std::vector<float> output(2048 * 2);
        while (mixing.load()) mixFrames(output.data(), 2048);
    });
    int failedCalls = 0;
    for (int i = 0; i < 50; ++i) {
        failedCalls += resetBufferStream(hash) != PlayerErrors::noError;
        // The mixer stops before the last delivery, so that source is still
        // whole when it is measured below.
        if (i == 49) {
            mixing.store(false);
            mixer.join();
        }
        // Ends on the 22.05 kHz source.
        const std::vector<unsigned char> &source = i % 2 == 0 ? fast : slow;
        failedCalls += addAudioDataStream(
                           hash, source.data(),
                           static_cast<unsigned int>(source.size())) !=
                       PlayerErrors::noError;
    }
    EXPECT(failedCalls == 0, "%d resets or deliveries failed", failedCalls);

    // The last reset seeked the voice to the start of the last source. A
    // voice adopts a format as it mixes: not yet done for that one.
    mixBlocks(4);
    const float rate = voiceBaseRate(voice);
    EXPECT(rate == 22050, "the voice should end on the last source's rate, "
                          "got %.0f", rate);
    const int count = crossings();
    EXPECT(normalSpeed(count),
           "the last source should keep its pitch: %d crossings (1x is about "
           "116)", count);
    stop(voice);
    disposeSound(hash);
}
} // namespace

int main(int argc, char **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string only = argc > 1 ? argv[1] : "";
    const auto want = [&](const char *name) { return only.empty() || only == name; };

    if (want("reset")) withEngine(resetKeepsUpWithTheSource);
    if (want("play-before-data")) withEngine(speedSetBeforeTheFirstChunk);
    if (want("speed-seek")) withEngine(speedKeptAcrossASeek);
    if (want("reset-mixing")) withEngine(resetWhileAnotherThreadMixes);

    std::printf("\n%d assertions, %d failures\n", gAssertions, gFailures);
    return gFailures ? 1 : 0;
}

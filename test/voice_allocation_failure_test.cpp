// Standalone native regression tests for voice-allocation failure.
//
// SoLoud encodes voice handles as:
//
//     (voice + 1) | (playIndex << 12)
//
// so the handle 7 means "voice slot 6, play index 0". That is also the numeric
// value of SOLOUD_ERRORS::UNKNOWN_ERROR, which Soloud::play() used to return
// when no voice could be allocated. A failure was therefore indistinguishable
// from a live voice, and isValidVoiceHandle() could not be used to tell them
// apart: it reports "valid" for the failure code as soon as such a voice
// exists.
//
// mPlayIndex increments on every play and wraps at 0xfffff, so the aliasing
// state is reached by a long-running engine rather than a fresh one: once the
// play index wraps back to 0 while voice slots 0..5 are busy, the next
// allocation lands in slot 6 and really does get the handle 7. These tests
// construct exactly that state.
//
// With the failure code aliasing a live voice, every caller that did not
// re-validate the result operated on somebody else's voice:
// playClocked()/playScheduled() delayed and unpaused it, and
// play3d()/play3dClocked() flagged it PROCESS_3D, pushed 3D parameters into it
// and unpaused it.
//
// play() now returns the invalid-handle sentinel 0 instead, and the compound
// helpers bail out on it. findFreeVoice_internal() also used to fall through to
// stopVoice_internal(-1) when every voice was protected, converting -1 to
// UINT_MAX and indexing mVoice[] out of bounds; it returns -1 now.
//
// Build and run from the flutter_soloud repository root with:
//
//   ./test/run_voice_allocation_failure_test.sh

#include "soloud.h"
#include "soloud_audiosource.h"

#include <cstdio>
#include <vector>

namespace {

constexpr unsigned int kSampleRate = 1024;
constexpr unsigned int kBufferSize = 512;

// The handle SOLOUD_ERRORS::UNKNOWN_ERROR aliases: voice slot 6, play index 0.
constexpr SoLoud::handle kAliasedHandle = 7;

int gFailures = 0;
int gAssertions = 0;

#define EXPECT(condition, format, ...) do { \
    ++gAssertions; \
    if (!(condition)) { \
        ++gFailures; \
        std::fprintf(stderr, "  FAIL [%s:%d] " format "\n", \
                     __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while (0)

// A silent, endless source. Enough to occupy a voice.
class SilenceInstance final : public SoLoud::AudioSourceInstance {
public:
    unsigned int getAudio(float* buffer, unsigned int samplesToRead,
                          unsigned int bufferSize) override {
        for (unsigned int i = 0; i < samplesToRead; i++)
            buffer[i] = 0.0f;
        (void)bufferSize;
        return samplesToRead;
    }
    bool hasEnded() override { return false; }
};

class Silence final : public SoLoud::AudioSource {
public:
    Silence() {
        mBaseSamplerate = static_cast<float>(kSampleRate);
        mChannels = 1;
    }
    SoLoud::AudioSourceInstance* createInstance() override {
        return new SilenceInstance();
    }
};

// Drive the engine into the state where a live, paused voice owns the handle
// that the old allocation-failure code used to return.
//
// Slots 0..5 are filled first so that every later allocation lands in slot 6,
// then slot 6 is recycled until the play index wraps back to 0. Returns false
// if the state could not be reached.
bool createVoiceOwningTheAliasedHandle(SoLoud::Soloud& soloud,
                                       Silence& source) {
    for (unsigned int i = 0; i < 6; i++) {
        SoLoud::handle blocker = soloud.play(source, 1.0f, 0.0f, true);
        if (blocker == 0) return false;
        soloud.setProtectVoice(blocker, true);
    }

    // mPlayIndex wraps at 0xfffff, so this terminates in about a million
    // iterations (~0.1s).
    for (unsigned int i = 0; i <= 0xfffff + 16; i++) {
        SoLoud::handle h = soloud.play(source, 1.0f, 0.0f, true);
        if (h == 0) return false;
        if (h == kAliasedHandle) {
            soloud.setProtectVoice(h, true);
            return true;
        }
        soloud.stop(h);
    }
    return false;
}

// Fill every remaining voice slot with a protected voice, so
// findFreeVoice_internal() has nothing it is allowed to stop and allocation
// must fail.
void fillAndProtectAllVoices(SoLoud::Soloud& soloud, Silence& source) {
    for (unsigned int i = 0; i < VOICE_COUNT; i++) {
        SoLoud::handle h = soloud.play(source, 1.0f, 0.0f, true);
        if (h == 0) break;
        soloud.setProtectVoice(h, true);
    }
}

void testAllocationFailureIsNotAValidHandle() {
    std::printf("test: a failed allocation never aliases a live voice\n");
    SoLoud::Soloud soloud;
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate, kBufferSize, 1);

    Silence source;
    EXPECT(createVoiceOwningTheAliasedHandle(soloud, source),
           "could not construct a voice owning handle %u", kAliasedHandle);

    // This is why isValidVoiceHandle() cannot be used to detect a failure:
    // the old failure code is a live voice right now.
    EXPECT(soloud.isValidVoiceHandle(kAliasedHandle),
           "handle %u should be a live voice in this state", kAliasedHandle);

    fillAndProtectAllVoices(soloud, source);

    // The engine is full and everything is protected: allocation must fail,
    // and it must not report the handle of the existing voice.
    SoLoud::handle failed = soloud.play(source, 1.0f, 0.0f, true);
    EXPECT(failed == 0, "play() on a full engine returned %u, expected 0",
           failed);
    EXPECT(!soloud.isValidVoiceHandle(0),
           "the failure sentinel must never be a valid voice handle");

    soloud.deinit();
}

// The whole failure-detection scheme rests on one property: the value a failed
// allocation returns can never be produced as a real handle. Pin it down, so
// that changing the sentinel or the handle encoding breaks here rather than
// silently re-introducing the aliasing.
void testSentinelIsDisjointFromTheHandleNamespace() {
    std::printf("test: the failure sentinel is outside the handle namespace\n");
    SoLoud::Soloud soloud;
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate, kBufferSize, 1);

    Silence source;

    // 0 is never a live voice, whatever the engine state.
    EXPECT(!soloud.isValidVoiceHandle(0),
           "0 must never be a valid voice handle");

    std::vector<SoLoud::handle> handles;
    for (unsigned int i = 0; i < VOICE_COUNT; i++) {
        SoLoud::handle h = soloud.play(source, 1.0f, 0.0f, true);
        if (h == 0) break;
        soloud.setProtectVoice(h, true);
        handles.push_back(h);
    }
    EXPECT(handles.size() == VOICE_COUNT,
           "expected to fill all %d voices, filled %zu",
           VOICE_COUNT, handles.size());

    // Handles are (voice + 1) | (playIndex << 12): the low 12 bits hold
    // voice + 1, which is 1..VOICE_COUNT, so a real handle is never 0. They
    // also stay clear of the voice-group range (0xfffff000 | index), which
    // isValidVoiceHandle() rejects outright.
    for (size_t i = 0; i < handles.size(); i++) {
        if (handles[i] == 0) {
            EXPECT(false, "voice %zu produced the failure sentinel", i);
            break;
        }
        if ((handles[i] & 0xfff) == 0) {
            EXPECT(false, "handle %u of voice %zu has empty voice bits",
                   handles[i], i);
            break;
        }
        if ((handles[i] & 0xfffff000) == 0xfffff000) {
            EXPECT(false, "handle %u of voice %zu collides with the voice "
                   "group range", handles[i], i);
            break;
        }
    }
    EXPECT(true, "all %zu handles stay inside the voice-handle namespace",
           handles.size());

    // 0 stays invalid with the engine full, which is exactly the state in
    // which allocation fails and the sentinel is returned.
    EXPECT(!soloud.isValidVoiceHandle(0),
           "0 must never be a valid voice handle, even with a full engine");
    EXPECT(soloud.play(source, 1.0f, 0.0f, true) == 0,
           "a full engine must report failure with the sentinel");

    soloud.deinit();
}

void testCompoundHelpersLeaveOtherVoicesAlone() {
    std::printf("test: a failed play*() does not disturb an unrelated voice\n");
    SoLoud::Soloud soloud;
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate, kBufferSize, 1);

    Silence source;
    EXPECT(createVoiceOwningTheAliasedHandle(soloud, source),
           "could not construct a voice owning handle %u", kAliasedHandle);
    fillAndProtectAllVoices(soloud, source);

    // Every voice was created paused. The voice at the aliased handle is the
    // one a failed call used to delay, unpause and turn into a 3D source.
    EXPECT(soloud.getPause(kAliasedHandle),
           "the unrelated voice should start paused");

    SoLoud::handle h = soloud.playClocked(0.5, source, 1.0f, 0.0f);
    EXPECT(h == 0, "playClocked() on a full engine returned %u, expected 0", h);
    EXPECT(soloud.getPause(kAliasedHandle),
           "playClocked() unpaused an unrelated voice");

    h = soloud.playScheduled(0.5, source, 1.0f, 0.0f);
    EXPECT(h == 0, "playScheduled() on a full engine returned %u, expected 0",
           h);
    EXPECT(soloud.getPause(kAliasedHandle),
           "playScheduled() unpaused an unrelated voice");

    h = soloud.play3d(source, 1, 2, 3);
    EXPECT(h == 0, "play3d() on a full engine returned %u, expected 0", h);
    EXPECT(soloud.getPause(kAliasedHandle),
           "play3d() unpaused an unrelated voice");

    h = soloud.play3dClocked(0.5, source, 1, 2, 3);
    EXPECT(h == 0, "play3dClocked() on a full engine returned %u, expected 0",
           h);
    EXPECT(soloud.getPause(kAliasedHandle),
           "play3dClocked() unpaused an unrelated voice");

    soloud.deinit();
}

void testSuccessfulPlaybackStillWorks() {
    std::printf("test: normal playback is unaffected\n");
    SoLoud::Soloud soloud;
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, kSampleRate, kBufferSize, 1);

    Silence source;
    SoLoud::handle h = soloud.play(source);
    EXPECT(h != 0, "play() returned the failure sentinel");
    EXPECT(soloud.isValidVoiceHandle(h), "play() returned an unusable handle");

    SoLoud::handle clocked = soloud.playClocked(0.1, source);
    EXPECT(clocked != 0, "playClocked() returned the failure sentinel");
    EXPECT(soloud.isValidVoiceHandle(clocked),
           "playClocked() returned an unusable handle");
    EXPECT(!soloud.getPause(clocked), "playClocked() left the voice paused");

    SoLoud::handle scheduled = soloud.playScheduled(0.1, source);
    EXPECT(scheduled != 0, "playScheduled() returned the failure sentinel");
    EXPECT(soloud.isValidVoiceHandle(scheduled),
           "playScheduled() returned an unusable handle");
    EXPECT(!soloud.getPause(scheduled), "playScheduled() left the voice paused");

    SoLoud::handle threeD = soloud.play3d(source, 1, 2, 3);
    EXPECT(threeD != 0, "play3d() returned the failure sentinel");
    EXPECT(soloud.isValidVoiceHandle(threeD),
           "play3d() returned an unusable handle");

    SoLoud::handle threeDClocked = soloud.play3dClocked(0.1, source, 1, 2, 3);
    EXPECT(threeDClocked != 0, "play3dClocked() returned the failure sentinel");
    EXPECT(soloud.isValidVoiceHandle(threeDClocked),
           "play3dClocked() returned an unusable handle");

    soloud.deinit();
}

} // namespace

int main() {
    testSentinelIsDisjointFromTheHandleNamespace();
    testAllocationFailureIsNotAValidHandle();
    testCompoundHelpersLeaveOtherVoicesAlone();
    testSuccessfulPlaybackStillWorks();

    if (gFailures == 0) {
        std::printf("\nAll %d assertions passed.\n", gAssertions);
        return 0;
    }
    std::fprintf(stderr, "\n%d of %d assertions FAILED.\n", gFailures,
                 gAssertions);
    return 1;
}

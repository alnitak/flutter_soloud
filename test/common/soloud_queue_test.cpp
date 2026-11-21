// Standalone native tests for SoLoud::Queue (src/soloud/src/core/
// soloud_queue.cpp), mixed through the engine with the null backend.
//
// The queue plays its sources one after the other at the queue's own rate
// and channel count (setParams). Cases:
//
//   concat        sources play back to back with no gap or overlap, sample
//                 for sample;
//   count         getQueueCount() counts the sources still queued and drops
//                 as each finishes; isCurrentlyPlaying() follows the head;
//   full          the 33rd source (SOLOUD_QUEUE_MAX = 32) is refused with
//                 OUT_OF_MEMORY and the first 32 all play;
//   formats       a source at another rate plays at the queue's rate; a
//                 stereo source in a mono queue and a mono source in a
//                 stereo queue stay in bounds (ASan) and finite;
//   drain_restart a queue that runs dry ends its voice; play() on a queue
//                 that is no longer playing; playing the queue again
//                 restarts it;
//   destroyed     a source destroyed while its instance waits in the queue;
//   starving      a source that returns 0 samples without ending.
//
// No audio device and no assets are used.
//
//   ./test/common/soloud_queue_test.sh [--asan] [--strict] [case]

#include "soloud.h"
#include "soloud_queue.h"

#include "../support/c_tests_checks.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace {

constexpr unsigned int kRate = 44100;
constexpr unsigned int kBlock = 512;

std::set<const void *> gDestroyed;
int gGetAudioOnDestroyedSource = 0;

class SeqSource;

/// Plays a planar buffer once. Optionally returns 0 samples, without
/// ending, for its first [starveCalls] getAudio calls.
class SeqInstance : public SoLoud::AudioSourceInstance {
 public:
  SeqInstance(SeqSource *parent);
  unsigned int getAudio(float *buf, unsigned int n,
                        unsigned int bufSize) override;
  bool hasEnded() override;
  const SeqSource *mParentPtr;
  size_t mPos = 0;
  long mCalls = 0;
};

class SeqSource : public SoLoud::AudioSource {
 public:
  SeqSource(std::vector<float> planar, unsigned int channels, float rate,
            long starveCalls = 0)
      : mData(std::move(planar)), mStarve(starveCalls) {
    mChannels = channels;
    mBaseSamplerate = rate;
    mFrames = mData.size() / channels;
  }
  ~SeqSource() override {
    stop();
    gDestroyed.insert(this);
  }
  SoLoud::AudioSourceInstance *createInstance() override {
    gDestroyed.erase(this);
    return new SeqInstance(this);
  }
  std::vector<float> mData;
  size_t mFrames;
  long mStarve;
  long mMaxCallsPerMix = 0;
};

SeqInstance::SeqInstance(SeqSource *parent) : mParentPtr(parent) {}

unsigned int SeqInstance::getAudio(float *buf, unsigned int n,
                                   unsigned int bufSize) {
  if (gDestroyed.count(mParentPtr)) {  // never dereference a dead parent
    gGetAudioOnDestroyedSource++;
    mPos = (size_t)-1;
    return 0;
  }
  const SeqSource *p = mParentPtr;
  mCalls++;
  if (mCalls <= p->mStarve) return 0;
  const size_t left = p->mFrames - mPos;
  const unsigned int k = (unsigned int)std::min<size_t>(n, left);
  for (unsigned int c = 0; c < p->mChannels; c++)
    std::memcpy(buf + c * bufSize, &p->mData[c * p->mFrames + mPos],
                k * sizeof(float));
  mPos += k;
  return k;
}

bool SeqInstance::hasEnded() {
  if (mPos == (size_t)-1) return true;
  if (gDestroyed.count(mParentPtr)) return true;
  return mPos >= mParentPtr->mFrames;
}

/// A ramp of [frames] distinct values, offset by [base] so sources differ.
std::vector<float> ramp(size_t frames, float base, unsigned int channels = 1) {
  std::vector<float> v(frames * channels);
  for (unsigned int c = 0; c < channels; c++)
    for (size_t i = 0; i < frames; i++)
      v[c * frames + i] =
          (c == 0 ? 1.f : -1.f) * (base + 0.0001f * (float)(i % 1000));
  return v;
}

struct Engine {
  SoLoud::Soloud soloud;
  unsigned int channels;
  explicit Engine(unsigned int ch) : channels(ch) {
    soloud.init(0, SoLoud::Soloud::NULLDRIVER, kRate, kBlock, ch);
  }
  ~Engine() { soloud.deinit(); }
  /// Mixes [frames] frames; returns them interleaved.
  std::vector<float> mix(size_t frames) {
    std::vector<float> out(frames * channels);
    for (size_t pos = 0; pos < frames; pos += kBlock) {
      const unsigned int n = (unsigned int)std::min<size_t>(kBlock, frames - pos);
      soloud.mix(out.data() + pos * channels, n);
    }
    return out;
  }
};

/// Index of the first sample whose magnitude exceeds [eps].
long firstNonZero(const std::vector<float> &v, unsigned int stride = 1,
                  float eps = 1e-7f) {
  for (size_t i = 0; i < v.size(); i += stride)
    if (std::fabs(v[i]) > eps) return (long)(i / stride);
  return -1;
}

void concat() {
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  e.soloud.play(q);
  // Lengths that end mid-block, a 1-frame source and a block-sized one.
  const size_t lens[] = {700, 1, 512, 1333, 64};
  std::vector<std::unique_ptr<SeqSource>> srcs;
  std::vector<float> expected;
  for (int i = 0; i < 5; i++) {
    srcs.emplace_back(new SeqSource(ramp(lens[i], 0.1f * (i + 1)), 1, kRate));
    CHECK(q.play(*srcs.back()) == SoLoud::SO_NO_ERROR, "queue source %d", i);
    expected.insert(expected.end(), srcs.back()->mData.begin(),
                    srcs.back()->mData.end());
  }
  std::vector<float> out = e.mix(8192);
  const long start = firstNonZero(out);
  CHECK(start >= 0 && start <= 2, "first sample at %ld", start);
  if (start < 0) return;
  // The engine's voice gain (mono pan law, global volume) is a constant
  // factor: take it from the first sample.
  const float g = out[start] / expected[0];
  size_t mismatches = 0, firstBad = 0;
  for (size_t i = 0; i < expected.size(); i++) {
    if (std::fabs(out[start + i] - g * expected[i]) > 1e-5f) {
      if (!mismatches) firstBad = i;
      mismatches++;
    }
  }
  CHECK(mismatches == 0,
        "%zu samples differ from the concatenation (first at %zu: %g vs %g)",
        mismatches, firstBad, out[start + firstBad], g * expected[firstBad]);
  bool silentAfter = true;
  for (size_t i = start + expected.size(); i < out.size(); i++)
    if (out[i] != 0.f) silentAfter = false;
  CHECK(silentAfter, "silence after the last source");
}

void count() {
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  SoLoud::handle h = e.soloud.play(q);
  SeqSource a(ramp(1000, 0.1f), 1, kRate), b(ramp(1000, 0.2f), 1, kRate),
      c(ramp(1000, 0.3f), 1, kRate);
  CHECK(q.getQueueCount() == 0, "empty queue counts 0");
  q.play(a);
  q.play(b);
  q.play(c);
  CHECK(q.getQueueCount() == 3, "3 queued, count %u", q.getQueueCount());
  CHECK(q.isCurrentlyPlaying(a) && !q.isCurrentlyPlaying(b),
        "head is the first source");
  e.mix(1024);  // a (1000) done
  CHECK(q.getQueueCount() == 2, "after 1024 frames count %u",
        q.getQueueCount());
  CHECK(q.isCurrentlyPlaying(b), "b is now the head");
  e.mix(1024);  // b done
  CHECK(q.getQueueCount() == 1, "after 2048 frames count %u",
        q.getQueueCount());
  e.mix(1024);
  CHECK(q.getQueueCount() == 0, "all played, count %u", q.getQueueCount());
  CHECK(!q.isCurrentlyPlaying(c), "nothing playing");
  e.mix(1024);
  CHECK(!e.soloud.isValidVoiceHandle(h), "drained queue voice ends");
}

void full() {
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  e.soloud.play(q);
  std::vector<std::unique_ptr<SeqSource>> srcs;
  for (int i = 0; i < SOLOUD_QUEUE_MAX; i++) {
    srcs.emplace_back(new SeqSource(ramp(100, 0.01f * (i + 1)), 1, kRate));
    CHECK(q.play(*srcs.back()) == SoLoud::SO_NO_ERROR, "source %d accepted",
          i);
  }
  SeqSource extra(ramp(100, 0.9f), 1, kRate);
  CHECK(q.play(extra) == SoLoud::OUT_OF_MEMORY, "33rd source refused");
  CHECK(q.getQueueCount() == SOLOUD_QUEUE_MAX, "count %u",
        q.getQueueCount());
  std::vector<float> out = e.mix(4096);
  const long start = firstNonZero(out);
  bool ok = start >= 0;
  const float g = ok ? out[start] / srcs[0]->mData[0] : 0.f;
  for (int i = 0; ok && i < SOLOUD_QUEUE_MAX; i++)
    for (int k = 0; k < 100; k++)
      if (std::fabs(out[start + i * 100 + k] - g * srcs[i]->mData[k]) > 1e-5f)
        ok = false;
  CHECK(ok, "all 32 sources play in order");
  CHECK(std::fabs(out[start + 3200]) == 0.f, "the refused one does not");
  // Wrap-around: the ring indices restart; a new batch plays too.
  // (A drained queue ends its voice, so each round plays it again.)
  SoLoud::Queue q2;
  q2.setParams((float)kRate, 1);
  for (int round = 0; round < 3; round++) {
    e.soloud.play(q2);
    for (int i = 0; i < 20; i++) q2.play(*srcs[i]);
    e.mix(2048 + 512);
    CHECK(q2.getQueueCount() == 0, "round %d drained, count %u", round,
          q2.getQueueCount());
  }
}

void formats() {
  // A 22050 Hz source in a 44100 Hz queue plays at the queue's rate (the
  // queue never resamples its sources): its frames come out one per output
  // frame, twice as fast.
  {
    Engine e(1);
    SoLoud::Queue q;
    q.setParams((float)kRate, 1);
    e.soloud.play(q);
    SeqSource half(ramp(2000, 0.2f), 1, kRate / 2);
    q.play(half);
    std::vector<float> out = e.mix(4096);
    const long start = firstNonZero(out);
    long last = -1;
    for (size_t i = 0; i < out.size(); i++)
      if (out[i] != 0.f) last = (long)i;
    CHECK(start >= 0 && last - start + 1 == 2000,
          "2000 frames at 22050 Hz last %ld output frames at 44100 Hz",
          last - start + 1);
  }
  // Stereo source in a mono queue: only channel 0 is heard; nothing writes
  // out of bounds (ASan).
  {
    Engine e(2);
    SoLoud::Queue q;
    q.setParams((float)kRate, 1);
    e.soloud.play(q);
    SeqSource st(ramp(3000, 0.3f, 2), 2, kRate);
    q.play(st);
    std::vector<float> out = e.mix(4096);
    CHECK(ctest::allFinite(out), "stereo-in-mono finite");
    CHECK(ctest::peak(out.data(), out.size()) < 1.0f, "stereo-in-mono bounded");
  }
  // Mono source in a stereo queue after a stereo one.
  {
    Engine e(2);
    SoLoud::Queue q;
    q.setParams((float)kRate, 2);
    e.soloud.play(q);
    SeqSource st(ramp(1024, 0.5f, 2), 2, kRate);
    SeqSource mono(std::vector<float>(2048, 0.f), 1, kRate);
    q.play(st);
    q.play(mono);
    std::vector<float> out = e.mix(4096);
    CHECK(ctest::allFinite(out), "mono-in-stereo finite");
    double rightDuringMono = 0;
    for (size_t f = 1024 + 600; f < 1024 + 2048 - 600; f++)
      rightDuringMono = std::max(rightDuringMono, (double)std::fabs(out[f * 2 + 1]));
    if (rightDuringMono > 0)
      NOTE("a silent mono source in a stereo queue leaves channel 1 with the "
           "previous source's samples (peak %.3f): the queue requires sources "
           "with its own channel count",
           rightDuringMono);
  }
}

void drainRestart() {
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  SoLoud::handle h = e.soloud.play(q);
  SeqSource a(ramp(800, 0.1f), 1, kRate), b(ramp(800, 0.6f), 1, kRate);
  q.play(a);
  e.mix(2048);
  CHECK(!e.soloud.isValidVoiceHandle(h), "drained queue ended its voice");
  // The queue is no longer playing: play() should refuse (it refuses when
  // the queue was never played) or the source should be heard.
  const SoLoud::result r = q.play(b);
  std::vector<float> out = e.mix(2048);
  const bool heard = firstNonZero(out) >= 0;
  CHECK(r != SoLoud::SO_NO_ERROR || heard,
        "Queue::play() on a queue whose voice ended returns %d (no error) "
        "but the source is never heard", (int)r);
  // Playing the queue again restarts it with what is queued.
  SoLoud::handle h2 = e.soloud.play(q);
  CHECK(e.soloud.isValidVoiceHandle(h2), "queue plays again");
  if (r == SoLoud::SO_NO_ERROR) {
    std::vector<float> out2 = e.mix(2048);
    CHECK(firstNonZero(out2) >= 0, "restarted queue plays the queued source");
  }
  // And new sources go through the restarted queue.
  SeqSource c(ramp(500, 0.4f), 1, kRate);
  e.mix(2048);
  SoLoud::handle h3 = e.soloud.play(q);
  CHECK(q.play(c) == SoLoud::SO_NO_ERROR, "queue after restart");
  std::vector<float> out3 = e.mix(2048);
  CHECK(firstNonZero(out3) >= 0, "source heard after restart");
  (void)h3;
}

void destroyed() {
  gGetAudioOnDestroyedSource = 0;
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  e.soloud.play(q);
  SeqSource a(ramp(1000, 0.1f), 1, kRate);
  auto *b = new SeqSource(ramp(1000, 0.2f), 1, kRate);
  q.play(a);
  q.play(*b);
  delete b;  // ~AudioSource stops b's voices, but b's instance is in the queue
  e.mix(4096);
  CHECK(gGetAudioOnDestroyedSource == 0,
        "a source destroyed while queued is still played: the queued "
        "instance reads its freed parent (%d getAudio calls)",
        gGetAudioOnDestroyedSource);
}

void starving() {
  // A source may return fewer samples than asked, even 0, while it has not
  // ended (e.g. a stream waiting for data); the mixer pads with silence.
  // The queue instead calls it again in a loop within the same mix, under
  // the audio mutex. This source returns 0 for its first 200000 calls.
  Engine e(1);
  SoLoud::Queue q;
  q.setParams((float)kRate, 1);
  e.soloud.play(q);
  SeqSource s(ramp(100, 0.2f), 1, kRate, 200000);
  q.play(s);
  e.mix(kBlock);
  // SeqInstance counts its calls; read them through the queue's ring.
  long calls = 0;
  if (q.mCount > 0)
    calls = static_cast<SeqInstance *>(q.mSource[q.mReadIndex])->mCalls;
  else
    calls = 200001;  // consumed entirely within one mix
  CHECK(calls <= 4,
        "one mix() of %u frames called a starving source's getAudio %ld "
        "times: QueueInstance::getAudio must not spin while the source "
        "returns 0 without ending", kBlock, calls);
}

}  // namespace

int main(int argc, char **argv) {
  ctest::parseArgs(argc, argv);
  if (ctest::selected("concat")) concat();
  if (ctest::selected("count")) count();
  if (ctest::selected("full")) full();
  if (ctest::selected("formats")) formats();
  if (ctest::selected("drain_restart")) drainRestart();
  if (ctest::selected("destroyed")) destroyed();
  if (ctest::selected("starving")) starving();
  return ctest::summary("soloud_queue_test");
}

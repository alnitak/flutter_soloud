#include "realtime_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace SoLoud {

namespace {

constexpr size_t kRampFrames = 64;

// Fixed-point fraction bits of AudioSourceInstance::mSrcOffset (see
// FIXPOINT_FRAC_BITS in soloud.cpp).
constexpr unsigned int kSrcOffsetFracBits = 20;

struct CosineDecayTable {
  alignas(16) float values[kRampFrames];
  CosineDecayTable() noexcept {
    for (size_t i = 0; i < kRampFrames; ++i) {
      values[i] =
          0.5f * (1.0f + std::cos(static_cast<float>(M_PI) *
                                  static_cast<float>(i + 1) /
                                  static_cast<float>(kRampFrames)));
    }
  }
};

const CosineDecayTable kCosineDecay64{};

inline float cosineDecay(size_t i, size_t rampLen) noexcept {
  return 0.5f * (1.0f + std::cos(static_cast<float>(M_PI) *
                                 static_cast<float>(i + 1) /
                                 static_cast<float>(rampLen)));
}

} // namespace

// /////////////////////////////////////////////////////////////////////////////
// RealtimeStreamInstance
// /////////////////////////////////////////////////////////////////////////////

RealtimeStreamInstance::RealtimeStreamInstance(RealtimeStream *aParent)
    : mParent(aParent) {
  mFlags |= AudioSourceInstance::REALTIME_DIRECT;
  mLastSample.fill(0.0f);
  if (mParent != nullptr) {
    mBaseSamplerate = mParent->mBaseSamplerate;
    mSamplerate = mBaseSamplerate;
    mChannels = mParent->mChannels;
    // A new voice plays whatever is queued contiguously: markers left behind
    // by an earlier voice do not apply to it.
    mMarkerCursor = mParent->mNextMarkerSeq.load(std::memory_order_relaxed);
    mParent->mVoiceStarved.store(true, std::memory_order_relaxed);
    mParent->mInstance.store(this, std::memory_order_release);
  }
}

RealtimeStreamInstance::~RealtimeStreamInstance() {
  if (mParent != nullptr) {
    RealtimeStreamInstance *expected = this;
    mParent->mInstance.compare_exchange_strong(expected, nullptr,
                                               std::memory_order_acq_rel);
  }
}

static const char kRealtimeStreamSnapshotKind = 0;

SourceStateSnapshot *RealtimeStreamInstance::captureSourceState() {
  auto *snap = new RealtimeStreamSnapshot();
  snap->mKind = &kRealtimeStreamSnapshotKind;
  captureSourceStateInto(snap);
  return snap;
}

bool RealtimeStreamInstance::captureSourceStateInto(SourceStateSnapshot *aState) {
  if (aState == nullptr || aState->mKind != &kRealtimeStreamSnapshotKind) {
    return false;
  }
  auto *snap = static_cast<RealtimeStreamSnapshot *>(aState);
  snap->mReadFrame =
      mParent != nullptr ? mParent->mRingBuffer.getReadFrame() : 0;
  snap->mSilentFramesSinceUnderrun = mSilentFramesSinceUnderrun;
  snap->mMarkerCursor = mMarkerCursor;
  snap->mSubQuantumDelayFrames = mSubQuantumDelayFrames;
  snap->mWasUnderrun = mWasUnderrun;
  snap->mLastSample = mLastSample;
  return true;
}

void RealtimeStreamInstance::restoreSourceState(SourceStateSnapshot *aState) {
  const auto *snap = static_cast<const RealtimeStreamSnapshot *>(aState);
  if (snap == nullptr) {
    return;
  }
  if (mParent != nullptr) {
    mParent->mRingBuffer.setReadFrame(snap->mReadFrame);
  }
  mSilentFramesSinceUnderrun = snap->mSilentFramesSinceUnderrun;
  mMarkerCursor = snap->mMarkerCursor;
  mSubQuantumDelayFrames = snap->mSubQuantumDelayFrames;
  mWasUnderrun = snap->mWasUnderrun;
  mLastSample = snap->mLastSample;
  if (mParent != nullptr) {
    mParent->mVoiceStarved.store(mSilentFramesSinceUnderrun > 0,
                                 std::memory_order_relaxed);
  }
}

void RealtimeStreamInstance::fadeToSilence(float *aBuffer,
                                           unsigned int aOffset,
                                           size_t aFrames,
                                           unsigned int aBufferSize) {
  if (aFrames == 0) {
    return;
  }
  const size_t rampLen = std::min<size_t>(aFrames, kRampFrames);
  for (unsigned int ch = 0; ch < mChannels; ++ch) {
    float *dst = aBuffer + ch * aBufferSize + aOffset;
    const float last = mLastSample[ch];
    mLastSample[ch] = 0.0f;
    if (std::fabs(last) <= 1e-6f) {
      std::memset(dst, 0, sizeof(float) * aFrames);
      continue;
    }
    if (rampLen == kRampFrames) {
      size_t i = 0;
#if defined(SOLOUD_RT_SIMD_SSE2)
      const __m128 vLast = _mm_set1_ps(last);
      for (; i + 4 <= kRampFrames; i += 4) {
        const __m128 d = _mm_load_ps(kCosineDecay64.values + i);
        _mm_storeu_ps(dst + i, _mm_mul_ps(vLast, d));
      }
#elif defined(SOLOUD_RT_SIMD_NEON)
      for (; i + 4 <= kRampFrames; i += 4) {
        const float32x4_t d = vld1q_f32(kCosineDecay64.values + i);
        vst1q_f32(dst + i, vmulq_n_f32(d, last));
      }
#elif defined(SOLOUD_RT_SIMD_WASM)
      const v128_t vLast = wasm_f32x4_splat(last);
      for (; i + 4 <= kRampFrames; i += 4) {
        const v128_t d = wasm_v128_load(kCosineDecay64.values + i);
        wasm_v128_store(dst + i, wasm_f32x4_mul(vLast, d));
      }
#endif
      for (; i < kRampFrames; ++i) {
        dst[i] = last * kCosineDecay64.values[i];
      }
    } else {
      for (size_t i = 0; i < rampLen; ++i) {
        dst[i] = last * cosineDecay(i, rampLen);
      }
    }
    if (aFrames > rampLen) {
      std::memset(dst + rampLen, 0, sizeof(float) * (aFrames - rampLen));
    }
  }
}

void RealtimeStreamInstance::rampInFromLastSample(float *aBuffer,
                                                  unsigned int aOffset,
                                                  size_t aFrames,
                                                  unsigned int aBufferSize) {
  const size_t rampLen = std::min<size_t>(aFrames, kRampFrames);
  for (unsigned int ch = 0; ch < mChannels; ++ch) {
    const float last = mLastSample[ch];
    if (std::fabs(last) <= 1e-6f) {
      continue;
    }
    float *dst = aBuffer + ch * aBufferSize + aOffset;
    if (rampLen == kRampFrames) {
      size_t i = 0;
#if defined(SOLOUD_RT_SIMD_SSE2)
      const __m128 vLast = _mm_set1_ps(last);
      for (; i + 4 <= kRampFrames; i += 4) {
        const __m128 d = _mm_load_ps(kCosineDecay64.values + i);
        const __m128 cur = _mm_loadu_ps(dst + i);
        _mm_storeu_ps(dst + i, _mm_add_ps(cur, _mm_mul_ps(vLast, d)));
      }
#elif defined(SOLOUD_RT_SIMD_NEON)
      for (; i + 4 <= kRampFrames; i += 4) {
        const float32x4_t d = vld1q_f32(kCosineDecay64.values + i);
        const float32x4_t cur = vld1q_f32(dst + i);
        vst1q_f32(dst + i, vmlaq_n_f32(cur, d, last));
      }
#elif defined(SOLOUD_RT_SIMD_WASM)
      const v128_t vLast = wasm_f32x4_splat(last);
      for (; i + 4 <= kRampFrames; i += 4) {
        const v128_t d = wasm_v128_load(kCosineDecay64.values + i);
        const v128_t cur = wasm_v128_load(dst + i);
        wasm_v128_store(dst + i,
                        wasm_f32x4_add(cur, wasm_f32x4_mul(vLast, d)));
      }
#endif
      for (; i < kRampFrames; ++i) {
        dst[i] += last * kCosineDecay64.values[i];
      }
    } else {
      for (size_t i = 0; i < rampLen; ++i) {
        dst[i] += last * cosineDecay(i, rampLen);
      }
    }
    mLastSample[ch] = 0.0f;
  }
}

unsigned int RealtimeStreamInstance::getAudio(float *aBuffer,
                                              unsigned int aSamplesToRead,
                                              unsigned int aBufferSize) {
  if (aBuffer == nullptr || mChannels == 0 || aSamplesToRead == 0) {
    return 0;
  }

  if (mParent == nullptr) {
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      std::memset(aBuffer + ch * aBufferSize, 0,
                  sizeof(float) * aSamplesToRead);
    }
    return aSamplesToRead;
  }

  RealtimeStream &stream = *mParent;
  RealtimeRingBuffer &ring = stream.mRingBuffer;
  unsigned int pos = 0;
  bool underran = false;
  bool rechecked = false;

  while (pos < aSamplesToRead) {
    const unsigned int remaining = aSamplesToRead - pos;

    // 1. A timeline marker at the read cursor. Checked before any pending
    // delay: a delay always belongs to a gap marker already applied at this
    // frame, so a marker still pending here is a reset recorded later, and
    // it cancels that delay just as the live reset did.
    const uint64_t readFrame = ring.getReadFrame();
    const size_t mi = stream.findPendingMarker(readFrame, mMarkerCursor);
    const RealtimeStream::TimelineMarker *marker =
        mi < stream.mMarkerCount ? &stream.mMarkers[mi] : nullptr;
    if (marker != nullptr && marker->frame == readFrame) {
      if (marker->reset) {
        // Replay of a reset: skip the frames it discarded.
        mSubQuantumDelayFrames = 0;
        ring.setReadFrame(marker->skipTo);
        mWasUnderrun = true;
      } else {
        // The gap is measured from when the earlier audio ran out; underrun
        // silence already emitted since then counts towards it.
        const uint64_t gap =
            marker->gapFrames > mSilentFramesSinceUnderrun
                ? marker->gapFrames - mSilentFramesSinceUnderrun
                : 0;
        mSubQuantumDelayFrames = static_cast<uint32_t>(std::min<uint64_t>(
            gap, std::numeric_limits<uint32_t>::max()));
      }
      mSilentFramesSinceUnderrun = 0;
      mMarkerCursor = marker->seq + 1;
      continue;
    }

    // 2. Scheduled silence before the next ring frame.
    if (mSubQuantumDelayFrames > 0) {
      const unsigned int n =
          std::min<uint32_t>(mSubQuantumDelayFrames, remaining);
      fadeToSilence(aBuffer, pos, n, aBufferSize);
      mSubQuantumDelayFrames -= n;
      pos += n;
      continue;
    }

    // 3. Ring data, up to the next marker.
    size_t want = remaining;
    if (marker != nullptr) {
      want = static_cast<size_t>(
          std::min<uint64_t>(want, marker->frame - readFrame));
    }
    const size_t got = ring.popPlanar(aBuffer + pos, want, aBufferSize);
    if (got > 0) {
      // If recovering from a non-zero DC level at underrun, smoothly decay
      // the residual offset so there is no step click, while preserving full
      // transient amplitude when starting from zero.
      if (mWasUnderrun) {
        rampInFromLastSample(aBuffer, pos, got, aBufferSize);
      }
      for (unsigned int ch = 0; ch < mChannels; ++ch) {
        mLastSample[ch] = aBuffer[ch * aBufferSize + pos + got - 1];
      }
      mWasUnderrun = false;
      mSilentFramesSinceUnderrun = 0;
      pos += static_cast<unsigned int>(got);
      continue;
    }

    // 4. Dry. Publish the starvation before a final look at the ring: this
    // fence pairs with the one in RealtimeStream::pushTimed(), so either the
    // producer sees mVoiceStarved and places its frames under the audio
    // mutex, or this re-check sees the frames it just pushed.
    if (!rechecked) {
      rechecked = true;
      stream.mVoiceStarved.store(true, std::memory_order_relaxed);
      std::atomic_thread_fence(std::memory_order_seq_cst);
      continue;
    }

    if (!mWasUnderrun) {
      mWasUnderrun = true;
      Soloud *engine =
          stream.mEngine != nullptr ? stream.mEngine : stream.mSoloud;
      if (engine == nullptr || !engine->mRemixing) {
        stream.mUnderrunCount.fetch_add(1, std::memory_order_relaxed);
      }
    }
    fadeToSilence(aBuffer, pos, remaining, aBufferSize);
    mSilentFramesSinceUnderrun += remaining;
    pos = aSamplesToRead;
    underran = true;
  }

  if (!underran) {
    stream.mVoiceStarved.store(false, std::memory_order_relaxed);
  }
  return aSamplesToRead;
}

bool RealtimeStreamInstance::hasEnded() {
  // Real-time streams stay active until explicitly stopped or disposed.
  return false;
}

result RealtimeStreamInstance::rewind() {
  // Called by AudioSourceInstance::seek() with the audio mutex held.
  if (mParent != nullptr) {
    mParent->discardUnreadLocked();
  }
  mStreamPosition = 0.0;
  mSilentFramesSinceUnderrun = 0;
  mSubQuantumDelayFrames = 0;
  mWasUnderrun = true;
  mLastSample.fill(0.0f);
  return SO_NO_ERROR;
}

// /////////////////////////////////////////////////////////////////////////////
// RealtimeStream
// /////////////////////////////////////////////////////////////////////////////

RealtimeStream::RealtimeStream() { mFlags |= AudioSource::SINGLE_INSTANCE; }

RealtimeStream::~RealtimeStream() { stop(); }

PlayerErrors RealtimeStream::init(Soloud *aEngine, unsigned int sampleRate,
                                  unsigned int channels, BufferType format,
                                  unsigned int bufferCapacityFrames,
                                  unsigned int historyFrames) {
  if (sampleRate == 0 || channels == 0 || channels > MAX_CHANNELS ||
      bufferCapacityFrames == 0) {
    return PlayerErrors::invalidParameter;
  }
  if (format != BufferType::PCM_F32LE &&
      format != BufferType::PCM_F32LE_PLANAR &&
      format != BufferType::PCM_S16LE) {
    return PlayerErrors::invalidParameter;
  }

  mEngine = aEngine;
  mFormat = format;
  mBaseSamplerate = static_cast<float>(sampleRate);
  mChannels = channels;
  mUnderrunCount.store(0, std::memory_order_relaxed);
  mMarkerCount = 0;

  // The history window must cover every frame a retroactive re-mix can
  // rewind over. The render ring holds up to renderAhead + one quantum of
  // mixed audio, and the checkpoint a re-mix restores can be up to one more
  // quantum before the playhead; convert that span to stream frames and add
  // a resampler block on either side. A larger requested window is
  // honoured.
  size_t history = historyFrames != 0 ? historyFrames : 4096;
  if (mEngine != nullptr && mEngine->mRenderAheadFrames > 0) {
    double window = static_cast<double>(mEngine->mRenderAheadFrames) +
                    2.0 * static_cast<double>(mEngine->mBufferSize);
    if (mEngine->mSamplerate > 0 && sampleRate > mEngine->mSamplerate) {
      window = window * sampleRate / static_cast<double>(mEngine->mSamplerate);
    }
    history = std::max<size_t>(
        history,
        static_cast<size_t>(std::ceil(window)) + SAMPLE_GRANULARITY * 2);
  }
  mRingBuffer.init(channels, bufferCapacityFrames, history);
  return PlayerErrors::noError;
}

AudioSourceInstance *RealtimeStream::createInstance() {
  return new RealtimeStreamInstance(this);
}

unsigned int RealtimeStream::pushRaw(SampleFormat format, const void *data,
                                     unsigned int numFrames) noexcept {
  if (format == SampleFormat::Int16) {
    return static_cast<unsigned int>(mRingBuffer.pushInterleavedInt16(
        static_cast<const int16_t *>(data), numFrames));
  }
  const float *samples = static_cast<const float *>(data);
  if (mFormat == BufferType::PCM_F32LE_PLANAR) {
    return static_cast<unsigned int>(
        mRingBuffer.pushPlanarFloat32(samples, numFrames));
  }
  return static_cast<unsigned int>(
      mRingBuffer.pushInterleavedFloat32(samples, numFrames));
}

unsigned int RealtimeStream::pushDataFloat32(const float *data,
                                             unsigned int numFrames) {
  return pushTimed(PushTiming::Immediate, 0.0, SampleFormat::Float32, data,
                   numFrames);
}

unsigned int RealtimeStream::pushDataInt16(const int16_t *data,
                                           unsigned int numFrames) {
  return pushTimed(PushTiming::Immediate, 0.0, SampleFormat::Int16, data,
                   numFrames);
}

unsigned int RealtimeStream::pushDataClockedFloat32(double soundTime,
                                                    const float *data,
                                                    unsigned int numFrames) {
  return pushTimed(PushTiming::Clocked, soundTime, SampleFormat::Float32,
                   data, numFrames);
}

unsigned int RealtimeStream::pushDataScheduledFloat32(double engineTime,
                                                      const float *data,
                                                      unsigned int numFrames) {
  return pushTimed(PushTiming::Scheduled, engineTime, SampleFormat::Float32,
                   data, numFrames);
}

unsigned int RealtimeStream::pushTimed(PushTiming timing, double timeValue,
                                       SampleFormat format, const void *data,
                                       unsigned int numFrames) {
  if (data == nullptr || numFrames == 0) {
    return 0;
  }
  Soloud *engine = mEngine;
  if (engine == nullptr ||
      mInstance.load(std::memory_order_acquire) == nullptr) {
    // Not playing: nothing to place in time. The frames start when a voice
    // starts.
    return pushRaw(format, data, numFrames);
  }

  uint64_t writeBefore = 0;
  unsigned int alreadyWritten = 0;
  if (timing == PushTiming::Immediate) {
    // Lock-free fast path: a voice that is still being fed simply reads the
    // appended frames when it gets to them.
    writeBefore = mRingBuffer.getWriteFrame();
    alreadyWritten = pushRaw(format, data, numFrames);
    if (alreadyWritten == 0) {
      return 0;
    }
    // Pairs with the fence in RealtimeStreamInstance::getAudio(): either we
    // observe the voice's starvation here, or its final look at the ring
    // observes the frames just pushed.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    if (!mVoiceStarved.load(std::memory_order_relaxed)) {
      return alreadyWritten;
    }
  }

  engine->lockAudioMutex_internal();
  if (alreadyWritten == 0) {
    writeBefore = mRingBuffer.getWriteFrame();
  }
  const unsigned int written =
      placeLocked(*engine, timing, timeValue, format, data, numFrames,
                  writeBefore, alreadyWritten);
  engine->unlockAudioMutex_internal();
  return written;
}

unsigned int RealtimeStream::placeLocked(Soloud &engine, PushTiming timing,
                                         double timeValue, SampleFormat format,
                                         const void *data,
                                         unsigned int numFrames,
                                         uint64_t writeBefore,
                                         unsigned int alreadyWritten) {
  RealtimeStreamInstance *inst = mInstance.load(std::memory_order_acquire);
  const int slot = findVoiceSlotLocked(engine, inst);
  if (slot < 0 || (inst->mFlags & AudioSourceInstance::PAUSED) ||
      engine.mSamplerate == 0 || !(inst->mSamplerate > 0.0f)) {
    // No running voice to place the frames against: plain append.
    return alreadyWritten > 0 ? alreadyWritten
                              : pushRaw(format, data, numFrames);
  }
  if (alreadyWritten > 0 && mRingBuffer.getReadFrame() > writeBefore) {
    // The voice already started reading these frames in a mix that ran
    // between the push and this lock; they are placed.
    return alreadyWritten;
  }

  const double voiceSr = static_cast<double>(inst->mSamplerate);
  const double engineSr = static_cast<double>(engine.mSamplerate);

  // When should the new frames start?
  const time playhead = engine.playheadTimeLocked_internal();
  time target = playhead;
  if (timing == PushTiming::Scheduled) {
    target = std::max<time>(timeValue, playhead);
  } else if (timing == PushTiming::Clocked) {
    const unsigned int delaySamples =
        engine.getClockedDelaySamplesLocked_internal(timeValue);
    target = engine.mStreamTime + static_cast<time>(delaySamples) / engineSr;
  }

  // Frames cannot overlap the audio queued before them: they start at the
  // requested time, or when the queued audio ends if that is later. A later
  // start leaves a gap, recorded as a marker in front of the new frames.
  const time queuedEnd =
      queuedAudioEndTimeLocked(engine, *inst, writeBefore, voiceSr);
  const time start = std::max(target, queuedEnd);
  uint64_t gapFrames = 0;
  if (target > queuedEnd) {
    gapFrames = static_cast<uint64_t>(
        std::llround((target - queuedEnd) * voiceSr));
  }
  bool markerAdded = false;
  if (gapFrames > 0) {
    TimelineMarker marker;
    marker.frame = writeBefore;
    marker.gapFrames = gapFrames;
    if (appendMarkerLocked(marker)) {
      markerAdded = true;
    } else if (alreadyWritten == 0) {
      // Too many gaps pending: refuse the frames rather than play them at
      // the wrong time.
      return 0;
    }
  }

  const unsigned int written =
      alreadyWritten > 0 ? alreadyWritten : pushRaw(format, data, numFrames);
  if (written == 0) {
    if (markerAdded) {
      --mMarkerCount;
    }
    return 0;
  }

  // If the frames start inside the already rendered render-ahead window,
  // roll the mix back and re-render it so they are heard on time. The
  // re-mix replays the voice from the checkpoint through the same ring
  // frames and markers, so everything before `start` comes out unchanged.
  bool rolledBack = false;
  if (start < engine.mStreamTime) {
    const int ci = engine.retroactiveBegin_internal(start);
    if (ci >= 0) {
      refillReadAheadLocked(engine, *inst);
      engine.retroactiveEnd_internal(ci);
      rolledBack = true;
    }
  }
  if (!rolledBack) {
    refillReadAheadLocked(engine, *inst);
  }
  return written;
}

time RealtimeStream::queuedAudioEndTimeLocked(const Soloud &engine,
                                              const RealtimeStreamInstance &inst,
                                              uint64_t writeBefore,
                                              double voiceSr) const {
  const uint64_t readFrame = mRingBuffer.getReadFrame();

  // On the resampling path the voice pulls whole SAMPLE_GRANULARITY blocks:
  // frames it already pulled but has not mixed yet come first.
  uint64_t readAhead = 0;
  if (inst.mLeftoverSamples > 0) {
    const unsigned int srcPos = inst.mSrcOffset >> kSrcOffsetFracBits;
    if (srcPos < SAMPLE_GRANULARITY) {
      readAhead = SAMPLE_GRANULARITY - srcPos;
    }
  }

  // The engine time at which the voice next pulls from the ring.
  const time next =
      engine.mStreamTime +
      static_cast<time>(inst.mDelaySamples) /
          static_cast<time>(engine.mSamplerate) +
      static_cast<time>(readAhead + inst.mSubQuantumDelayFrames) / voiceSr;

  size_t mi = findPendingMarker(readFrame, inst.mMarkerCursor);
  time t = next;
  uint64_t pos = readFrame;

  if (inst.mSilentFramesSinceUnderrun > 0) {
    // Starved: the queued audio ran out this long ago.
    const time dataEnd =
        next - static_cast<time>(inst.mSilentFramesSinceUnderrun) / voiceSr;
    const bool markerAtCursor =
        mi < mMarkerCount && mMarkers[mi].frame == readFrame;
    if (readFrame >= writeBefore && !markerAtCursor) {
      return dataEnd;
    }
    if (markerAtCursor && !mMarkers[mi].reset) {
      // A gap counts from dataEnd but cannot end before the next pull.
      t = std::max<time>(
          dataEnd + static_cast<time>(mMarkers[mi].gapFrames) / voiceSr, next);
      ++mi;
    }
  }

  for (; mi < mMarkerCount && mMarkers[mi].frame < writeBefore; ++mi) {
    const TimelineMarker &m = mMarkers[mi];
    if (m.frame > pos) {
      t += static_cast<time>(m.frame - pos) / voiceSr;
      pos = m.frame;
    }
    if (m.reset) {
      pos = std::max(pos, m.skipTo);
    } else {
      t += static_cast<time>(m.gapFrames) / voiceSr;
    }
  }
  if (writeBefore > pos) {
    t += static_cast<time>(writeBefore - pos) / voiceSr;
  }
  return t;
}

void RealtimeStream::refillReadAheadLocked(Soloud &engine,
                                           RealtimeStreamInstance &inst) {
  // Resampling path only: if the voice's current SAMPLE_GRANULARITY block
  // ends in underrun silence that it pulled before these frames arrived,
  // regenerate the unconsumed part of that silence so the new frames are not
  // delayed until the next block.
  if (inst.mLeftoverSamples == 0 || inst.mResampleData[0] == nullptr ||
      inst.mSilentFramesSinceUnderrun == 0) {
    return;
  }
  const unsigned int srcPos = inst.mSrcOffset >> kSrcOffsetFracBits;
  if (srcPos >= SAMPLE_GRANULARITY ||
      mRingBuffer.getWriteFrame() <= mRingBuffer.getReadFrame()) {
    return;
  }
  const unsigned int tail = static_cast<unsigned int>(std::min<uint64_t>(
      inst.mSilentFramesSinceUnderrun, SAMPLE_GRANULARITY));
  const unsigned int refillStart =
      std::max(srcPos, SAMPLE_GRANULARITY - tail);
  if (refillStart >= SAMPLE_GRANULARITY) {
    return;
  }
  const unsigned int refillCount = SAMPLE_GRANULARITY - refillStart;
  // Those frames were counted as underrun silence; they are produced again
  // below from the updated timeline.
  inst.mSilentFramesSinceUnderrun -= refillCount;
  inst.getAudio(inst.mResampleData[0] + refillStart, refillCount,
                SAMPLE_GRANULARITY);
  for (unsigned int j = 0; j < FILTERS_PER_STREAM; ++j) {
    if (inst.mFilter[j] != nullptr) {
      inst.mFilter[j]->filter(inst.mResampleData[0] + refillStart, refillCount,
                              SAMPLE_GRANULARITY, inst.mChannels,
                              inst.mSamplerate, engine.mStreamTime);
    }
  }
}

void RealtimeStream::resetBuffer() {
  Soloud *engine = mEngine;
  if (engine != nullptr) {
    engine->lockAudioMutex_internal();
  }
  discardUnreadLocked();
  mUnderrunCount.store(0, std::memory_order_relaxed);
  if (engine != nullptr) {
    engine->unlockAudioMutex_internal();
  }
}

void RealtimeStream::discardUnreadLocked() {
  RealtimeStreamInstance *inst = mInstance.load(std::memory_order_acquire);
  const uint64_t readFrame = mRingBuffer.getReadFrame();
  const uint64_t writeFrame = mRingBuffer.getWriteFrame();
  const bool remixing = mEngine != nullptr && mEngine->mRemixing;

  bool resetMarker = false;
  uint64_t resetSeq = 0;
  if (!remixing) {
    // Gaps in front of discarded frames go with them.
    while (mMarkerCount > 0 && mMarkers[mMarkerCount - 1].frame >= readFrame) {
      --mMarkerCount;
    }
    // Record the discard so that a re-mix replaying from a checkpoint taken
    // before it skips the discarded frames instead of playing them.
    if (inst != nullptr && writeFrame > readFrame) {
      pruneMarkersLocked();
      if (mMarkerCount == kMaxTimelineMarkers) {
        // Every remaining marker is behind the read cursor, kept only for
        // re-mix replays: drop the oldest to make room.
        std::move(mMarkers.begin() + 1, mMarkers.begin() + mMarkerCount,
                  mMarkers.begin());
        --mMarkerCount;
      }
      TimelineMarker marker;
      marker.frame = readFrame;
      marker.skipTo = writeFrame;
      marker.reset = true;
      resetMarker = appendMarkerLocked(marker);
      if (resetMarker) {
        resetSeq = mMarkers[mMarkerCount - 1].seq;
      }
    }

    // Jumping the read cursor over the discarded frames frees their ring
    // space, so the producer may overwrite the frames consumed just before
    // this point, which a re-mix from an older checkpoint would replay.
    // Retire those checkpoints. The one at the current mix head (captured at
    // the end of the last mix, i.e. at this very point) stays usable: a
    // replay from it reaches the reset marker before reading anything.
    if (resetMarker && mEngine != nullptr && mEngine->isRenderAheadEnabled() &&
        mEngine->mSamplerate > 0) {
      const time resetTime =
          mEngine->mStreamTime - 0.5 / static_cast<time>(mEngine->mSamplerate);
      for (MixCheckpoint &cp : mEngine->mCheckpointPool) {
        if (cp.mSerial != 0 && cp.mTime < resetTime) {
          cp.mRestorable = false;
        }
      }
    }
  }

  mRingBuffer.reset();
  if (inst != nullptr) {
    inst->mSubQuantumDelayFrames = 0;
    inst->mSilentFramesSinceUnderrun = 0;
    inst->mWasUnderrun = true;
    if (resetMarker) {
      // Applied live just now.
      inst->mMarkerCursor = resetSeq + 1;
    }
  }
  // The voice will starve at the next mix unless frames arrive: route the
  // next push through the placement path.
  mVoiceStarved.store(true, std::memory_order_relaxed);
}

int RealtimeStream::findVoiceSlotLocked(
    const Soloud &engine, const RealtimeStreamInstance *inst) const {
  if (inst == nullptr) {
    return -1;
  }
  for (unsigned int i = 0; i < engine.mHighestVoice; ++i) {
    if (engine.mVoice[i] == inst) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

size_t RealtimeStream::findPendingMarker(uint64_t readFrame,
                                         uint64_t cursor) const noexcept {
  // Markers are sorted by both seq and frame. One the voice already applied
  // has seq < cursor; one behind the read cursor was skipped over (a reset).
  size_t i = 0;
  while (i < mMarkerCount &&
         (mMarkers[i].seq < cursor || mMarkers[i].frame < readFrame)) {
    ++i;
  }
  return i;
}

void RealtimeStream::pruneMarkersLocked() noexcept {
  // A re-mix can only rewind the read cursor to a frame still held in the
  // ring, so markers further back than the ring's capacity are unreachable.
  const uint64_t readFrame = mRingBuffer.getReadFrame();
  const uint64_t capacity = mRingBuffer.rawCapacityFrames();
  if (readFrame <= capacity) {
    return;
  }
  const uint64_t oldestReachable = readFrame - capacity;
  size_t drop = 0;
  while (drop < mMarkerCount && mMarkers[drop].frame < oldestReachable) {
    ++drop;
  }
  if (drop > 0) {
    std::move(mMarkers.begin() + drop, mMarkers.begin() + mMarkerCount,
              mMarkers.begin());
    mMarkerCount -= drop;
  }
}

bool RealtimeStream::appendMarkerLocked(const TimelineMarker &marker) noexcept {
  pruneMarkersLocked();
  if (mMarkerCount == kMaxTimelineMarkers) {
    return false;
  }
  mMarkers[mMarkerCount] = marker;
  mMarkers[mMarkerCount].seq =
      mNextMarkerSeq.fetch_add(1, std::memory_order_relaxed);
  ++mMarkerCount;
  return true;
}

} // namespace SoLoud

#include "realtime_stream.h"

#include "../player.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace SoLoud {

RealtimeStreamInstance::RealtimeStreamInstance(RealtimeStream *aParent)
    : mParent(aParent) {
  mLastSample.fill(0.0f);
  if (mParent != nullptr) {
    mBaseSamplerate = mParent->mBaseSamplerate;
    mSamplerate = mBaseSamplerate;
    mChannels = mParent->mChannels;
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

SourceStateSnapshot *RealtimeStreamInstance::captureSourceState() {
  auto *snap = new RealtimeStreamSnapshot();
  snap->mReadFrame =
      mParent != nullptr ? mParent->mRingBuffer.getReadFrame() : 0;
  snap->mSilentFramesSinceUnderrun = mSilentFramesSinceUnderrun;
  snap->mSubQuantumDelayFrames = mSubQuantumDelayFrames;
  snap->mWasUnderrun = mWasUnderrun;
  snap->mLastSample = mLastSample;
  return snap;
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
  mSubQuantumDelayFrames = snap->mSubQuantumDelayFrames;
  mWasUnderrun = snap->mWasUnderrun;
  mLastSample = snap->mLastSample;
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

  unsigned int delayThisCall = 0;
  if (mSubQuantumDelayFrames > 0) {
    delayThisCall = std::min<uint32_t>(mSubQuantumDelayFrames, aSamplesToRead);
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      std::memset(aBuffer + ch * aBufferSize, 0, sizeof(float) * delayThisCall);
    }
    mSubQuantumDelayFrames -= delayThisCall;
    if (delayThisCall == aSamplesToRead) {
      mSilentFramesSinceUnderrun += delayThisCall;
      return aSamplesToRead;
    }
  }

  const unsigned int remainingToRead = aSamplesToRead - delayThisCall;
  const size_t readFrames = mParent->mRingBuffer.popPlanar(
      aBuffer + delayThisCall, remainingToRead, aBufferSize);

  constexpr size_t kRampFrames = 64;

  if (readFrames > 0) {
    // If recovering from a non-zero DC level at underrun, smoothly decay the
    // residual offset over the first 64 frames so there is no step click while
    // preserving full transient amplitude when starting from zero.
    if (mWasUnderrun) {
      const size_t rampLen = std::min<size_t>(readFrames, kRampFrames);
      for (unsigned int ch = 0; ch < mChannels; ++ch) {
        const float last = mLastSample[ch];
        if (std::fabs(last) > 1e-6f) {
          float *dst = aBuffer + ch * aBufferSize + delayThisCall;
          for (size_t i = 0; i < rampLen; ++i) {
            const float decay =
                0.5f * (1.0f + std::cos(static_cast<float>(M_PI) *
                                        static_cast<float>(i + 1) /
                                        static_cast<float>(rampLen)));
            dst[i] += last * decay;
          }
          mLastSample[ch] = 0.0f;
        }
      }
    }

    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      mLastSample[ch] =
          aBuffer[ch * aBufferSize + delayThisCall + readFrames - 1];
    }
  }

  if (readFrames == remainingToRead) {
    mWasUnderrun = false;
    mSilentFramesSinceUnderrun = 0;
  } else {
    const size_t tailSilence = remainingToRead - readFrames;
    const size_t tailStart = delayThisCall + readFrames;

    if (!mWasUnderrun) {
      mWasUnderrun = true;
      if (mParent->mSoloud == nullptr || !mParent->mSoloud->mRemixing) {
        mParent->mUnderrunCount.fetch_add(1, std::memory_order_relaxed);
      }
    }

    const size_t rampLen = std::min<size_t>(tailSilence, kRampFrames);
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      float *dst = aBuffer + ch * aBufferSize + tailStart;
      const float last = mLastSample[ch];
      if (std::fabs(last) > 1e-6f && rampLen > 0) {
        for (size_t i = 0; i < rampLen; ++i) {
          const float decay =
              0.5f * (1.0f + std::cos(static_cast<float>(M_PI) *
                                      static_cast<float>(i + 1) /
                                      static_cast<float>(rampLen)));
          dst[i] = last * decay;
        }
        if (tailSilence > rampLen) {
          std::memset(dst + rampLen, 0,
                      sizeof(float) * (tailSilence - rampLen));
        }
        mLastSample[ch] = 0.0f;
      } else {
        std::memset(dst, 0, sizeof(float) * tailSilence);
        mLastSample[ch] = 0.0f;
      }
    }

    mSilentFramesSinceUnderrun += tailSilence;
  }

  return aSamplesToRead;
}

bool RealtimeStreamInstance::hasEnded() {
  // Real-time streams stay active until explicitly stopped or disposed.
  return false;
}

result RealtimeStreamInstance::rewind() {
  if (mParent != nullptr) {
    mParent->resetBuffer();
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

PlayerErrors RealtimeStream::init(Player *aPlayer, ActiveSound *aParent,
                                  unsigned int sampleRate,
                                  unsigned int channels, BufferType format,
                                  unsigned int bufferCapacityFrames) {
  if ((aPlayer == nullptr) != (aParent == nullptr)) {
    return PlayerErrors::nullPointer;
  }
  if (sampleRate == 0 || channels == 0 || channels > MAX_CHANNELS ||
      bufferCapacityFrames == 0) {
    return PlayerErrors::invalidParameter;
  }
  if (format != BufferType::PCM_F32LE &&
      format != BufferType::PCM_F32LE_PLANAR &&
      format != BufferType::PCM_S16LE) {
    return PlayerErrors::invalidParameter;
  }

  mThePlayer = aPlayer;
  mParent = aParent;
  mFormat = format;
  mBaseSamplerate = static_cast<float>(sampleRate);
  mChannels = channels;
  mUnderrunCount.store(0, std::memory_order_relaxed);

  // Reserve a history window large enough to cover the engine's render-ahead
  // window plus one full resampler block (SAMPLE_GRANULARITY).
  size_t historyFrames = 4096;
  if (mThePlayer != nullptr && mThePlayer->soloud.mRenderAheadFrames > 0) {
    historyFrames = std::max<size_t>(
        4096, mThePlayer->soloud.mRenderAheadFrames + SAMPLE_GRANULARITY * 2);
  }
  mRingBuffer.init(channels, bufferCapacityFrames, historyFrames);
  return PlayerErrors::noError;
}

AudioSourceInstance *RealtimeStream::createInstance() {
  return new RealtimeStreamInstance(this);
}

unsigned int RealtimeStream::pushFloat32Internal(
    const float *data, unsigned int numFrames) noexcept {
  if (mFormat == BufferType::PCM_F32LE_PLANAR) {
    return static_cast<unsigned int>(
        mRingBuffer.pushPlanarFloat32(data, numFrames));
  }
  return static_cast<unsigned int>(
      mRingBuffer.pushInterleavedFloat32(data, numFrames));
}

unsigned int RealtimeStream::pushDataFloat32(const float *data,
                                             unsigned int numFrames) {
  if (data == nullptr || numFrames == 0) {
    return 0;
  }
  const uint64_t writeBefore = mRingBuffer.getWriteFrame();
  const unsigned int written = pushFloat32Internal(data, numFrames);
  if (written > 0) {
    injectIntoPlayingWindow(writeBefore, false, 0.0, false);
  }
  return written;
}

unsigned int RealtimeStream::pushDataInt16(const int16_t *data,
                                           unsigned int numFrames) {
  if (data == nullptr || numFrames == 0) {
    return 0;
  }
  const uint64_t writeBefore = mRingBuffer.getWriteFrame();
  const unsigned int written = static_cast<unsigned int>(
      mRingBuffer.pushInterleavedInt16(data, numFrames));
  if (written > 0) {
    injectIntoPlayingWindow(writeBefore, false, 0.0, false);
  }
  return written;
}

unsigned int RealtimeStream::pushDataClockedFloat32(double soundTime,
                                                    const float *data,
                                                    unsigned int numFrames) {
  if (data == nullptr || numFrames == 0) {
    return 0;
  }
  const uint64_t writeBefore = mRingBuffer.getWriteFrame();
  const unsigned int written = pushFloat32Internal(data, numFrames);
  if (written > 0) {
    injectIntoPlayingWindow(writeBefore, true, soundTime, true);
  }
  return written;
}

unsigned int RealtimeStream::pushDataScheduledFloat32(double engineTime,
                                                      const float *data,
                                                      unsigned int numFrames) {
  if (data == nullptr || numFrames == 0) {
    return 0;
  }
  const uint64_t writeBefore = mRingBuffer.getWriteFrame();
  const unsigned int written = pushFloat32Internal(data, numFrames);
  if (written > 0) {
    injectIntoPlayingWindow(writeBefore, true, engineTime, false);
  }
  return written;
}

void RealtimeStream::resetBuffer() { mRingBuffer.reset(); }

void RealtimeStream::injectIntoPlayingWindow(uint64_t writeFrameBeforePush,
                                             bool useExplicitTime,
                                             double explicitTime,
                                             bool isClocked) {
  if (mThePlayer == nullptr || mParent == nullptr) {
    return;
  }
  Soloud &soloud = mThePlayer->soloud;
  RealtimeStreamInstance *inst = mInstance.load(std::memory_order_acquire);
  if (inst == nullptr) {
    return;
  }

  // Fast path for continuous streaming: if this is an immediate push and the
  // voice has not experienced any underrun in the render-ahead window (meaning
  // the render-ahead window is already 100% populated with valid stream audio
  // up to mStreamTime), no retroactive rollback is needed at all.
  if (!useExplicitTime && inst->mSilentFramesSinceUnderrun == 0 &&
      !inst->mWasUnderrun) {
    return;
  }

  soloud.lockAudioMutex_internal();

  inst = mInstance.load(std::memory_order_acquire);
  if (inst == nullptr || mParent->handle.empty() ||
      soloud.mSamplerate == 0) {
    soloud.unlockAudioMutex_internal();
    return;
  }

  const handle h = mParent->handle.front().handle;
  const int slot = soloud.getVoiceFromHandle_internal(h);
  if (slot < 0 || soloud.mVoice[slot] != inst ||
      (inst->mFlags & AudioSourceInstance::PAUSED)) {
    soloud.unlockAudioMutex_internal();
    return;
  }

  const double voiceSr =
      inst->mSamplerate > 0.0f ? static_cast<double>(inst->mSamplerate)
                               : static_cast<double>(soloud.mSamplerate);

  // Determine the target engine time for the newly pushed frames.
  time targetEngineTime = soloud.playheadTimeLocked_internal();
  if (useExplicitTime) {
    if (isClocked) {
      const unsigned int delaySamples =
          soloud.getClockedDelaySamplesLocked_internal(explicitTime);
      targetEngineTime =
          soloud.mStreamTime +
          static_cast<time>(delaySamples) / static_cast<time>(soloud.mSamplerate);
    } else {
      targetEngineTime = explicitTime;
      const time playhead = soloud.playheadTimeLocked_internal();
      if (targetEngineTime < playhead) {
        targetEngineTime = playhead;
      }
    }
  }

  // If the target time is at or beyond the current mix write head (mStreamTime),
  // no already-rendered quantum needs to be rewritten. If the stream is
  // currently dry at mStreamTime, schedule the sub-quantum sample delay from
  // mStreamTime so future mix_internal() calls start reading at targetEngineTime.
  if (targetEngineTime >= soloud.mStreamTime) {
    if (mRingBuffer.getReadFrame() == writeFrameBeforePush &&
        targetEngineTime > soloud.mStreamTime) {
      const uint32_t futureDelay = static_cast<uint32_t>(
          std::llround((targetEngineTime - soloud.mStreamTime) * voiceSr));
      inst->mSubQuantumDelayFrames = futureDelay;
    }
    soloud.unlockAudioMutex_internal();
    return;
  }

  if (!soloud.isRenderAheadEnabled() || !soloud.mRenderRing.isInited()) {
    soloud.unlockAudioMutex_internal();
    return;
  }

  // If the stream only ran dry mid-window (after targetEngineTime), we only
  // need to roll back to the point where the underrun began.
  time effectiveEventTime = targetEngineTime;
  if (!useExplicitTime && inst->mSilentFramesSinceUnderrun > 0) {
    const time underrunStartTime =
        soloud.mStreamTime -
        static_cast<time>(inst->mSilentFramesSinceUnderrun) / voiceSr;
    if (underrunStartTime > effectiveEventTime &&
        underrunStartTime < soloud.mStreamTime) {
      effectiveEventTime = underrunStartTime;
    }
  }

  const int ci = soloud.retroactiveBegin_internal(effectiveEventTime);
  if (ci < 0) {
    soloud.unlockAudioMutex_internal();
    return;
  }

  const uint64_t cpReadFrame = mRingBuffer.getReadFrame();
  const uint64_t availBeforePush =
      (writeFrameBeforePush > cpReadFrame) ? (writeFrameBeforePush - cpReadFrame)
                                           : 0;
  const uint64_t offsetFrames =
      (targetEngineTime > soloud.mRemixCheckpointTime)
          ? static_cast<uint64_t>(std::llround(
                (targetEngineTime - soloud.mRemixCheckpointTime) * voiceSr))
          : 0;

  if (availBeforePush < offsetFrames) {
    // The buffer was already dry before targetEngineTime: fast-forward read
    // cursor to the newly pushed block and delay by offsetFrames from the
    // checkpoint so the first new sample lands at targetEngineTime.
    mRingBuffer.setReadFrame(writeFrameBeforePush);
    inst->mSubQuantumDelayFrames = static_cast<uint32_t>(offsetFrames);
  } else {
    // The buffer still had valid frames at the checkpoint and ran dry later in
    // the window: keep cpReadFrame so the remix replays [cpReadFrame,
    // writeFrameBeforePush) and seamlessly splices into the newly pushed block.
    inst->mSubQuantumDelayFrames = 0;
  }

  // If the restored checkpoint sits mid-SAMPLE_GRANULARITY block
  // (mLeftoverSamples > 0), the unconsumed portion of mResampleData[0] was
  // populated before this push. Refill the unconsumed slice from the updated
  // ring buffer so the next quantum's resampler sees the new samples.
  if (inst->mLeftoverSamples > 0 && inst->mResampleData[0] != nullptr) {
    const unsigned int srcPos =
        static_cast<unsigned int>(inst->mSrcOffset >> 20);
    if (srcPos < SAMPLE_GRANULARITY) {
      unsigned int refillStart = srcPos;
      if (availBeforePush >= offsetFrames &&
          availBeforePush < (SAMPLE_GRANULARITY - srcPos)) {
        refillStart = srcPos + static_cast<unsigned int>(availBeforePush);
        mRingBuffer.setReadFrame(writeFrameBeforePush);
      }
      if (refillStart < SAMPLE_GRANULARITY) {
        const unsigned int refillCount = SAMPLE_GRANULARITY - refillStart;
        inst->getAudio(inst->mResampleData[0] + refillStart, refillCount,
                       SAMPLE_GRANULARITY);
        for (unsigned int j = 0; j < FILTERS_PER_STREAM; ++j) {
          if (inst->mFilter[j] != nullptr) {
            inst->mFilter[j]->filter(inst->mResampleData[0] + refillStart,
                                     refillCount, SAMPLE_GRANULARITY,
                                     inst->mChannels, inst->mSamplerate,
                                     soloud.mStreamTime);
          }
        }
      }
    }
  }

  soloud.retroactiveEnd_internal(ci);
  soloud.unlockAudioMutex_internal();
}

} // namespace SoLoud

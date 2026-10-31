#pragma once

#ifndef REALTIME_STREAM_H
#define REALTIME_STREAM_H

#include "../active_sound.h"
#include "../enums.h"
#include "../soloud/include/soloud.h"
#include "realtime_ring_buffer.h"

#include <array>
#include <atomic>
#include <cstdint>

class Player;

namespace SoLoud {

class RealtimeStream;

struct RealtimeStreamSnapshot : public SourceStateSnapshot {
  uint64_t mReadFrame = 0;
  uint64_t mSilentFramesSinceUnderrun = 0;
  uint32_t mSubQuantumDelayFrames = 0;
  bool mWasUnderrun = true;
  std::array<float, MAX_CHANNELS> mLastSample{};
};

/// Active voice instance for a `RealtimeStream`. Only one instance per
/// `RealtimeStream` is active at a time (`AudioSource::SINGLE_INSTANCE`).
class RealtimeStreamInstance : public AudioSourceInstance {
public:
  explicit RealtimeStreamInstance(RealtimeStream *aParent);
  ~RealtimeStreamInstance() override;

  unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                        unsigned int aBufferSize) override;
  bool hasEnded() override;
  result rewind() override;

  SourceStateSnapshot *captureSourceState() override;
  void restoreSourceState(SourceStateSnapshot *aState) override;

  friend class RealtimeStream;

private:
  RealtimeStream *mParent = nullptr;
  uint64_t mSilentFramesSinceUnderrun = 0;
  uint32_t mSubQuantumDelayFrames = 0;
  bool mWasUnderrun = true;
  std::array<float, MAX_CHANNELS> mLastSample{};
};

/// Wait-free real-time PCM audio stream that feeds SoLoud's mixer directly
/// and supports instantaneous injection into the currently playing mixer
/// window (`mRenderRing`) via retroactive checkpoint re-mixing.
class RealtimeStream : public AudioSource {
public:
  RealtimeStream();
  ~RealtimeStream() override;

  PlayerErrors init(Player *aPlayer, ActiveSound *aParent,
                    unsigned int sampleRate, unsigned int channels,
                    BufferType format, unsigned int bufferCapacityFrames);

  AudioSourceInstance *createInstance() override;

  /// Push `numFrames` of float32 PCM (`PCM_F32LE` interleaved or
  /// `PCM_F32LE_PLANAR` planar according to `mFormat`) and immediately
  /// inject into the playing render-ahead window if needed.
  unsigned int pushDataFloat32(const float *data, unsigned int numFrames);

  /// Push `numFrames` of interleaved int16 PCM (`PCM_S16LE`) and immediately
  /// inject into the playing render-ahead window if needed.
  unsigned int pushDataInt16(const int16_t *data, unsigned int numFrames);

  /// Push `numFrames` of float32 PCM clocked to `soundTime` (like
  /// `playClocked`).
  unsigned int pushDataClockedFloat32(double soundTime, const float *data,
                                      unsigned int numFrames);

  /// Push `numFrames` of float32 PCM scheduled at `engineTime` (like
  /// `playScheduled`).
  unsigned int pushDataScheduledFloat32(double engineTime, const float *data,
                                        unsigned int numFrames);

  void resetBuffer();

  unsigned int getAvailableToRead() const noexcept {
    return static_cast<unsigned int>(mRingBuffer.availableToRead());
  }

  unsigned int getAvailableToWrite() const noexcept {
    return static_cast<unsigned int>(mRingBuffer.availableToWrite());
  }

  unsigned int getUnderrunCount() const noexcept {
    return mUnderrunCount.load(std::memory_order_relaxed);
  }

  BufferType getFormat() const noexcept { return mFormat; }

  friend class RealtimeStreamInstance;

private:
  unsigned int pushFloat32Internal(const float *data,
                                   unsigned int numFrames) noexcept;
  void injectIntoPlayingWindow(uint64_t writeFrameBeforePush,
                               bool useExplicitTime, double explicitTime,
                               bool isClocked);

  Player *mThePlayer = nullptr;
  ActiveSound *mParent = nullptr;
  BufferType mFormat = BufferType::PCM_F32LE;
  RealtimeRingBuffer mRingBuffer;
  std::atomic<RealtimeStreamInstance *> mInstance{nullptr};
  std::atomic<unsigned int> mUnderrunCount{0};
};

} // namespace SoLoud

#endif // REALTIME_STREAM_H

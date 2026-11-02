#pragma once

#ifndef REALTIME_STREAM_H
#define REALTIME_STREAM_H

#include "../enums.h"
#include "../soloud/include/soloud.h"
#include "realtime_ring_buffer.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace SoLoud {

class RealtimeStream;

struct RealtimeStreamSnapshot : public SourceStateSnapshot {
  uint64_t mReadFrame = 0;
  uint64_t mSilentFramesSinceUnderrun = 0;
  uint64_t mMarkerCursor = 0;
  uint32_t mSubQuantumDelayFrames = 0;
  bool mWasUnderrun = true;
  std::array<float, MAX_CHANNELS> mLastSample{};
};

/// Active voice instance for a `RealtimeStream`. Only one instance per
/// `RealtimeStream` is active at a time (`AudioSource::SINGLE_INSTANCE`).
///
/// All of its state is owned by the mixer: it is read and written only with
/// SoLoud's audio mutex held (the mixer holds it for every getAudio() call).
/// Producer threads never dereference an instance outside that mutex; they
/// only consult `RealtimeStream::mVoiceStarved`.
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
  /// Emit `aFrames` of silence at `aOffset`, decaying from the last emitted
  /// sample over at most 64 frames so there is no step click.
  void fadeToSilence(float *aBuffer, unsigned int aOffset, size_t aFrames,
                     unsigned int aBufferSize);
  /// Smoothly decay a residual DC offset left by an underrun into the first
  /// frames of newly read data.
  void rampInFromLastSample(float *aBuffer, unsigned int aOffset,
                            size_t aFrames, unsigned int aBufferSize);

  RealtimeStream *mParent = nullptr;
  /// Underrun silence emitted since the last ring frame was read. Scheduled
  /// gaps are not counted here.
  uint64_t mSilentFramesSinceUnderrun = 0;
  /// Sequence number of the next timeline marker this voice may apply;
  /// markers with a lower sequence number were applied (or predate it).
  uint64_t mMarkerCursor = 0;
  /// Scheduled silence still to emit before the next ring frame.
  uint32_t mSubQuantumDelayFrames = 0;
  bool mWasUnderrun = true;
  std::array<float, MAX_CHANNELS> mLastSample{};
};

/// Real-time PCM stream that feeds SoLoud's mixer directly and supports
/// instantaneous injection into the currently playing mixer window
/// (`mRenderRing`) via retroactive checkpoint re-mixing.
///
/// Threading: one producer thread pushes, the mixer consumes. Immediate pushes
/// to a voice that is not starved are lock-free. Every other operation that
/// has to agree with the mixer (timed pushes, splicing into the render-ahead
/// window, reset) runs with SoLoud's audio mutex held.
///
/// Timeline: a timed push (clocked or scheduled) that starts after the audio
/// already queued ends leaves a gap. The gap is recorded as a marker keyed to
/// the absolute ring frame it precedes, so the live voice and any retroactive
/// re-mix replaying from an older checkpoint both insert it at the same point.
/// A reset is recorded the same way, so a re-mix never replays discarded
/// frames.
class RealtimeStream : public AudioSource {
public:
  /// Maximum number of gap/reset markers that can be pending at once.
  static constexpr size_t kMaxTimelineMarkers = 256;

  RealtimeStream();
  ~RealtimeStream() override;

  /// Configure the stream. `aEngine` may be null (standalone use without
  /// playing-window injection). `historyFrames` is the minimum rewind window
  /// to retain; it is raised as needed to cover the engine's render-ahead
  /// window. 0 selects the default (4096 frames).
  PlayerErrors init(Soloud *aEngine, unsigned int sampleRate,
                    unsigned int channels, BufferType format,
                    unsigned int bufferCapacityFrames,
                    unsigned int historyFrames = 0);

  AudioSourceInstance *createInstance() override;

  /// Push `numFrames` of float32 PCM (`PCM_F32LE` interleaved or
  /// `PCM_F32LE_PLANAR` planar according to `mFormat`), appended after the
  /// queued audio. A starved voice resumes at the device playhead.
  unsigned int pushDataFloat32(const float *data, unsigned int numFrames);

  /// Push `numFrames` of interleaved int16 PCM (`PCM_S16LE`), appended after
  /// the queued audio. A starved voice resumes at the device playhead.
  unsigned int pushDataInt16(const int16_t *data, unsigned int numFrames);

  /// Push `numFrames` of float32 PCM clocked to `soundTime` (like
  /// `playClocked`).
  unsigned int pushDataClockedFloat32(double soundTime, const float *data,
                                      unsigned int numFrames);

  /// Push `numFrames` of float32 PCM scheduled at `engineTime` (like
  /// `playScheduled`). If audio queued earlier ends before `engineTime`, the
  /// stream stays silent until then; if it ends later, the new frames follow
  /// it directly.
  unsigned int pushDataScheduledFloat32(double engineTime, const float *data,
                                        unsigned int numFrames);

  /// Discard all unread frames, clear pending gaps and reset the underrun
  /// count. Serialized with the mixer through the audio mutex.
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

  size_t getHistoryFrames() const noexcept {
    return mRingBuffer.historyFrames();
  }

  friend class RealtimeStreamInstance;

private:
  enum class PushTiming { Immediate, Clocked, Scheduled };
  enum class SampleFormat { Float32, Int16 };

  struct TimelineMarker {
    /// Append order. Markers are sorted by both `seq` and `frame`.
    uint64_t seq = 0;
    /// Absolute ring frame the marker precedes.
    uint64_t frame = 0;
    /// Scheduled silence before `frame`, measured from the moment the audio
    /// queued before `frame` ran out.
    uint64_t gapFrames = 0;
    /// For a reset marker: the read cursor jumps here (frames in between
    /// were discarded).
    uint64_t skipTo = 0;
    bool reset = false;
  };

  unsigned int pushRaw(SampleFormat format, const void *data,
                       unsigned int numFrames) noexcept;
  unsigned int pushTimed(PushTiming timing, double timeValue,
                         SampleFormat format, const void *data,
                         unsigned int numFrames);
  unsigned int placeLocked(Soloud &engine, PushTiming timing,
                           double timeValue, SampleFormat format,
                           const void *data, unsigned int numFrames,
                           uint64_t writeBefore, unsigned int alreadyWritten);
  time queuedAudioEndTimeLocked(const Soloud &engine,
                                const RealtimeStreamInstance &inst,
                                uint64_t writeBefore, double voiceSr) const;
  void refillReadAheadLocked(Soloud &engine, RealtimeStreamInstance &inst);
  void discardUnreadLocked();
  int findVoiceSlotLocked(const Soloud &engine,
                          const RealtimeStreamInstance *inst) const;

  /// Index of the first marker not yet applied by a voice whose marker
  /// cursor is `cursor` and whose read cursor is at `readFrame`, or
  /// mMarkerCount if none. Audio mutex held.
  size_t findPendingMarker(uint64_t readFrame, uint64_t cursor) const noexcept;
  void pruneMarkersLocked() noexcept;
  bool appendMarkerLocked(const TimelineMarker &marker) noexcept;

  Soloud *mEngine = nullptr;
  BufferType mFormat = BufferType::PCM_F32LE;
  RealtimeRingBuffer mRingBuffer;
  std::atomic<RealtimeStreamInstance *> mInstance{nullptr};
  std::atomic<unsigned int> mUnderrunCount{0};
  /// True when the live voice ended its last mix starved (emitting underrun
  /// silence), so an immediate push must be placed under the audio mutex.
  /// Written by the mixer, read lock-free by the producer; see pushTimed().
  std::atomic<bool> mVoiceStarved{true};

  /// Pending gap/reset markers, sorted by frame. Audio mutex held.
  std::array<TimelineMarker, kMaxTimelineMarkers> mMarkers{};
  size_t mMarkerCount = 0;
  /// Sequence number for the next marker. Atomic only because a new
  /// instance reads it outside the audio mutex.
  std::atomic<uint64_t> mNextMarkerSeq{1};
};

} // namespace SoLoud

#endif // REALTIME_STREAM_H

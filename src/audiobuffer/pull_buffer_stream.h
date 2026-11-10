#pragma once

#ifndef PULL_BUFFER_STREAM_H
#define PULL_BUFFER_STREAM_H

#include "../active_sound.h"
#include "../dart_callback_gate.h"
#include "../enums.h"
#include "../player.h"
#include "../soloud/include/soloud.h"
#include "circular_float_buffer.h"
#include "metadata_ffi.h"
#include "stream_decoder.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class Player;

namespace SoLoud {

class PullBufferStream;

/// A seek or a rewind of the voice that waits for the decode mutex (see
/// PullBufferStreamInstance::deferMove()).
enum class PullStreamMove { None, Rewind, Seek };

/// One active voice for a PullBufferStream. Only one instance is supported per
/// source because the circular buffer is shared.
class PullBufferStreamInstance : public AudioSourceInstance {
public:
  explicit PullBufferStreamInstance(PullBufferStream *aParent);

  /// Produce the requested number of audio frames. Called from the audio
  /// thread.
  unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                        unsigned int aBufferSize) override;

  /// Seek to a target time. In-buffer seeks are instant; out-of-buffer seeks
  /// request new data from the computed encoded offset.
  result seek(double aSeconds, float *aScratch,
              unsigned int aScratchSize) override;

  result rewind() override;
  bool hasEnded() override;
  ~PullBufferStreamInstance() override;

private:
  /// The bodies of seek() and rewind(), with mParent->mDecodeMutex held.
  void seekLocked(double aSeconds);
  void rewindLocked();
  /// Leaves a seek (or a rewind, with [aSeconds] 0) that found the decode
  /// mutex held for getAudio() to apply. SoLoud seeks and rewinds with its
  /// audio mutex held, often on the mixer, and a delivery holds the decode
  /// mutex for its whole decode: waiting would stall the mixer (on the web's
  /// AudioWorklet thread a contended lock aborts).
  void deferMove(PullStreamMove aMove, double aSeconds);

  PullBufferStream *mParent;
};

/// AudioSource that requests encoded data on demand and stores decoded floats
/// in a circular buffer.
class PullBufferStream : public AudioSource {
public:
  PullBufferStream();
  ~PullBufferStream() override;

  PlayerErrors setPullBufferStream(
      Player *aPlayer, ActiveSound *aParent,
      unsigned int bufferSizeBytes, double bufferTriggerPosition,
      unsigned int sampleRate, unsigned int channels, BufferType format,
      uint64_t audioSizeBytes,
      dartOnBufferingCallback_t onBufferingCallback,
      dartOnMetadataCallback_t onMetadataCallback,
      dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
      dartOnAudioDurationCallback_t onAudioDurationCallback);

  void resetPullBufferStream();

  PlayerErrors addAudioData(const void *aData, unsigned int aDataLen,
                            uint64_t aOffset = 0);

  /// Get the current decoded buffer time range in seconds.
  void getBufferTimeRange(double &startTime, double &endTime) const;

  void callOnBufferingCallback(bool isBuffering, unsigned int handle,
                               double time);
  void callOnMetadataCallback(AudioMetadata &metadata);
  void callOnMoreDataIsNeededCallback(uint64_t offset);
  void callOnAudioDurationCallback(double duration);

  void clearDartCallbacks();

  AudioSourceInstance *createInstance() override;

  /// Not for the audio thread: both take mDecodeMutex.
  SoLoud::time getLength() const;
  SoLoud::time getBufferedLength() const;

  bool isValid() const { return !mIsDestroyed.load(); }
  void markForDestruction() { mIsDestroyed.store(true); }

  /// True when the audio thread has requested data but it has not arrived yet.
  bool isWaitingForData() const { return mWaitingForData.load(); }

  bool isBuffering() const { return mIsBuffering.load(); }
  bool isDataEnded() const { return mDataIsEnded.load(); }

  AudioMetadataFFI convertMetadataToFFI(const AudioMetadata &metadata) const;

  // Allow the instance to read from the buffer and trigger callbacks.
  friend class PullBufferStreamInstance;

private:
  enum class DurationProbeState {
    None,          ///< Duration is known at setup (PCM) or not applicable.
    Probing,       ///< Waiting for the first chunk to detect format.
    BuildingSeekTable, ///< Ogg format: fetching a few more sequential chunks
                   ///< so the seek table covers short seeks before the tail probe.
    TailRequested, ///< Ogg format: requested the tail to read last granule.
    Done           ///< Duration has been reported.
  };

  // Methods named *Locked, and the decode and probe helpers below, need
  // mDecodeMutex held by the caller (see its declaration).

  /// Pauses the voice when the decoded audio runs low and resumes it once
  /// enough is buffered again.
  ///
  /// Runs only on a thread delivering data (addAudioData()), after it has
  /// released mDecodeMutex, and never on the audio thread: it calls
  /// Player::getPause()/setPause(), which take SoLoud's audio mutex. The mixer
  /// holds that mutex while it mixes, and a seek holds it while it takes
  /// mDecodeMutex.
  void checkBuffering();
  /// Asks Dart for the next chunk if the buffer is running low. Decides under
  /// mDecodeMutex and calls back after releasing it. [aFromMixer]: only
  /// try-lock, and skip the request when a delivery holds the mutex (that
  /// delivery asks for more itself once it is done).
  void requestMoreDataIfNeeded(bool aFromMixer = false);
  /// The decision part of requestMoreDataIfNeeded(): true, with [aOffset] to
  /// request, when a request is due.
  bool nextDataRequestLocked(uint64_t &aOffset);
  /// [aUnderAudioMutex]: the caller is mixing (tryDecodePendingData()) or
  /// seeking the voice, with SoLoud's audio mutex held; see applyPendingSeek().
  void decodePendingDataLocked(bool aUnderAudioMutex = false);
  void tryDecodePendingData();
  /// Completes a pending seek once data at the target has been decoded. When
  /// the encoded offset becomes known only now, also moves the voice's play
  /// position: directly when [aUnderAudioMutex] (the position belongs to the
  /// mixer, which advances it every block), otherwise through
  /// mPendingStreamPosition for the mixer's next getAudio() to apply. It runs
  /// on the mixer too, so it leaves the buffering check to the delivery that
  /// brought the data (see mSeekCompletedSinceCheck).
  void applyPendingSeek(bool aUnderAudioMutex = false);
  void drainBacklog();
  void drainBacklogLocked();
  void probeDurationFromFirstChunk();
  void probeDurationFromOggTail(uint64_t aOffset);
  /// Records the duration; the caller reports it once it has released
  /// mDecodeMutex (see DeferredCallbacks).
  void finishDurationProbe(double duration);
  void setDataIsEndedLocked();

  /// The duration and metadata callbacks that work done under mDecodeMutex
  /// produced, to make once it is released: on the web they run Dart
  /// synchronously, and Dart may call back into the stream. Every thread that
  /// can produce them takes them before it unlocks and delivers them after.
  struct DeferredCallbacks {
    std::vector<AudioMetadata> metadata;
    bool hasDuration = false;
    double duration = 0.0;
  };
  DeferredCallbacks takeDeferredCallbacksLocked();
  void deliverDeferredCallbacks(DeferredCallbacks &aCallbacks);
  SoLoud::time getLengthLocked() const;

  /// Convert target time (seconds) to a byte offset in the encoded stream.
  /// For PCM this is exact; for compressed formats it uses the decoder
  /// metadata once known.
  uint64_t timeToEncodedByteOffsetLocked(double seconds);

  /// Reset the decoded playback window and stream offsets without zeroing the
  /// underlying storage.
  void resetBufferWindow(uint64_t decodedSamplesRead,
                         uint64_t decodedSamplesWritten,
                         double bufferStartTime,
                         uint64_t totalReceivedBytes,
                         uint64_t sequentialReadOffset);

  void resetProbeState();

  /// Reset the decoder state for an out-of-buffer seek whose data will be fed
  /// from [byteOffset] in the encoded file.
  void resetDecoderForSeek(uint64_t targetSample, uint64_t byteOffset);

  /// Current start/end of the decoded circular buffer in seconds.
  double getBufferStartTimeLocked() const;
  double getBufferEndTimeLocked() const;

  Player *mThePlayer;
  ActiveSound *mParent;

  CircularFloatBuffer mCircularBuffer;

  std::atomic<dartOnBufferingCallback_t> mOnBufferingCallback{nullptr};
  std::atomic<dartOnMetadataCallback_t> mOnMetadataCallback{nullptr};
  std::atomic<dartOnMoreDataIsNeededCallback_t> mOnMoreDataIsNeededCallback{
      nullptr};
  std::atomic<dartOnAudioDurationCallback_t> mOnAudioDurationCallback{nullptr};
  /// The Dart callback generation the four callables above were registered at.
  /// Checked on every invocation so a retirement makes them inert without
  /// having to reach into this object. See `src/dart_callback_gate.h`.
  std::atomic<uint64_t> mDartCallbackGeneration{dart_callbacks::kNoGeneration};

  std::unique_ptr<StreamDecoder> mStreamDecoder;

  PCMformat mPCMformat;
  double mBufferTriggerPosition = 0.0;
  uint64_t mAudioSizeBytes = 0; ///< Total encoded/PCM file size in bytes.
  uint64_t mSequentialReadOffset = 0; ///< Next byte offset for sequential requests.
  uint64_t mTotalReceivedBytes = 0; ///< Total encoded bytes ever added (including probes).
  // Read on the audio thread and written on the main thread, hence atomic.
  std::atomic<uint64_t> mDecodedSamplesRead{0};    // Total decoded samples consumed by playback.
  std::atomic<uint64_t> mDecodedSamplesWritten{0}; // Total decoded samples written to the buffer.
  double mBufferStartTime = 0.0;       // Logical start time of the valid decoded window; set by out-of-buffer seek.

  std::atomic<bool> mIsDestroyed{false};
  std::atomic<bool> mDataIsEnded{false};
  std::atomic<bool> mIsBuffering{false};
  std::atomic<bool> mWaitingForData{false};
  std::atomic<bool> mMetadataReceived{false};
  std::atomic<PullBufferStreamInstance *> mInstance{nullptr};
  std::atomic<bool> mPendingSeek{false};
  double mPendingSeekTime = 0.0;
  uint64_t mPendingSeekByteOffset = 0;
  /// Set when applyPendingSeek() completes a seek, cleared by the buffering
  /// check that follows. A delivery that only decodes (an empty one) runs the
  /// check when it is set; any other delivery runs it anyway.
  std::atomic<bool> mSeekCompletedSinceCheck{false};

  DurationProbeState mDurationProbeState = DurationProbeState::None;
  double mProbedDuration = 0.0; ///< Total duration in seconds, 0 if unknown.

  /// Ogg tail-probe bookkeeping.
  bool mTailProbeChunkSeen = false;          ///< True once the tail has been successfully parsed.
  std::atomic<uint64_t> mDurationProbeTailRangeStart{0}; ///< First byte of the requested tail range.
  std::atomic<uint64_t> mDurationProbeTailRangeEnd{0};   ///< One past the last byte of the file.
  std::atomic<uint64_t> mNextTailOffset{0};              ///< Next tail byte to request.
  int mSeekTableBuildChunks = 0;             ///< Sequential chunks fed while building the Ogg seek table.
  static constexpr int kMaxSeekTableBuildChunks = 16; ///< Cap for seek-table pre-build phase.
  std::vector<unsigned char> mTailProbeBuffer; ///< Accumulated tail bytes for Ogg probe.

  /// Guards every non-atomic member above and below that the mixer and the
  /// other threads share: the decoder, the format, the offsets and counters,
  /// the pending seek, the duration probe and the buffers below. A delivery,
  /// a reset and the Dart-thread getters take it. The mixer never waits for
  /// it: it only try-locks it while mixing, skipping that work when a
  /// delivery holds it, and a seek or a rewind, which SoLoud runs with its
  /// audio mutex held, is deferred when it finds it held (mDeferredMove).
  /// Nothing that holds it may wait for the audio mutex or sounds_mutex,
  /// and no callback runs under it: the Player calls
  /// (checkBuffering()) and every Dart callback, which on the web can re-enter
  /// the stream synchronously, run after it is released (see
  /// DeferredCallbacks).
  mutable std::mutex mDecodeMutex;
  /// A seek or rewind deferred by PullBufferStreamInstance::deferMove(), and
  /// the seek's target. Set and applied with the audio mutex held; a reset
  /// drops it.
  std::atomic<PullStreamMove> mDeferredMove{PullStreamMove::None};
  std::atomic<double> mDeferredSeekTime{0.0};
  DeferredCallbacks mDeferredCallbacks;
  /// A play position for the voice, resolved by a delivery (see
  /// applyPendingSeek()) and applied by the mixer's next getAudio().
  std::atomic<bool> mStreamPositionPending{false};
  std::atomic<double> mPendingStreamPosition{0.0};
  std::vector<unsigned char> mEncodedBuffer;
  std::vector<unsigned char> mDecodeScratch;
  std::vector<float> mDecodedBacklog;
  /// Size of mDecodedBacklog, mirrored in an atomic so lock-free readers
  /// (audio thread) do not race with vector mutations on the main thread.
  std::atomic<size_t> mDecodedBacklogSize{0};
};

} // namespace SoLoud

#endif // PULL_BUFFER_STREAM_H

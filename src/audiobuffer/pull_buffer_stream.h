#pragma once

#ifndef PULL_BUFFER_STREAM_H
#define PULL_BUFFER_STREAM_H

#include "../active_sound.h"
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
  PullBufferStream *mParent;
  /// Scratch for the interleaved read in getAudio(), reused across callbacks
  /// to avoid allocating on the audio thread.
  std::vector<float> mReadScratch;
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

  SoLoud::time getLength() const;
  SoLoud::time getBufferedLength() const;

  /// Convert target time (seconds) to a byte offset in the encoded stream.
  /// For PCM this is exact; for compressed formats it uses the decoder
  /// metadata once known.
  uint64_t timeToEncodedByteOffset(double seconds);

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

  void checkBuffering();
  void requestMoreDataIfNeeded();
  void decodePendingData();
  void decodePendingDataLocked();
  void tryDecodePendingData();
  void applyPendingSeek();
  void drainBacklog();
  void drainBacklogLocked();
  void probeDurationFromFirstChunk();
  void probeDurationFromOggTail(uint64_t aOffset);
  void finishDurationProbe(double duration);
  void setDataIsEnded();

  /// Reset the decoded playback window and stream offsets without zeroing the
  /// underlying storage.
  void resetBufferWindow(uint64_t decodedSamplesRead,
                         uint64_t decodedSamplesWritten,
                         double bufferStartTime,
                         uint64_t totalReceivedBytes,
                         uint64_t sequentialReadOffset);

  void resetProbeState();

  /// Reset the decoder state for an out-of-buffer seek.
  void resetDecoderForSeek(uint64_t targetSample);

  /// Current start/end of the decoded circular buffer in seconds.
  double getBufferStartTime() const;
  double getBufferEndTime() const;

  Player *mThePlayer;
  ActiveSound *mParent;

  CircularFloatBuffer mCircularBuffer;

  std::atomic<dartOnBufferingCallback_t> mOnBufferingCallback{nullptr};
  std::atomic<dartOnMetadataCallback_t> mOnMetadataCallback{nullptr};
  std::atomic<dartOnMoreDataIsNeededCallback_t> mOnMoreDataIsNeededCallback{
      nullptr};
  std::atomic<dartOnAudioDurationCallback_t> mOnAudioDurationCallback{nullptr};

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

  mutable std::mutex mDecodeMutex;
  std::vector<unsigned char> mEncodedBuffer;
  std::vector<unsigned char> mDecodeScratch;
  std::vector<float> mDecodedBacklog;
  /// Size of mDecodedBacklog, mirrored in an atomic so lock-free readers
  /// (audio thread) do not race with vector mutations on the main thread.
  std::atomic<size_t> mDecodedBacklogSize{0};
};

} // namespace SoLoud

#endif // PULL_BUFFER_STREAM_H

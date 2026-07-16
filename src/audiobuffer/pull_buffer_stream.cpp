#include "pull_buffer_stream.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../soloud_common.h"
#include "metadata_ffi.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

namespace SoLoud {

PullBufferStreamInstance::PullBufferStreamInstance(PullBufferStream *aParent)
    : mParent(aParent) {
  if (mParent != nullptr) {
    mParent->mInstance = this;
  }
}

PullBufferStreamInstance::~PullBufferStreamInstance() {
  if (mParent != nullptr) {
    mParent->mInstance = nullptr;
  }
}

unsigned int PullBufferStreamInstance::getAudio(float *aBuffer,
                                                unsigned int aSamplesToRead,
                                                unsigned int aBufferSize) {
  if (mParent == nullptr || !mParent->isValid()) {
    std::memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);
    return 0;
  }

  mBaseSamplerate = static_cast<float>(mParent->mPCMformat.sampleRate);
  mSamplerate = static_cast<float>(mParent->mPCMformat.sampleRate);
  mChannels = mParent->mPCMformat.channels;

  // Move any decoded samples that did not fit earlier into the circular
  // buffer so they become available for playback.
  mParent->drainBacklog();
  mParent->tryDecodePendingData();

  const size_t availableSamples = mParent->mCircularBuffer.available();
  const unsigned int requestedSamples = aSamplesToRead * mChannels;
  const size_t samplesToRead =
      std::min(static_cast<size_t>(requestedSamples), availableSamples);

  // No decoded samples available: zero the whole output buffer and ask for
  // more data. Do not call checkBuffering here: it runs on the audio thread
  // and would deadlock when calling getPause/getPosition.
  if (samplesToRead == 0) {
    std::memset(aBuffer, 0, sizeof(float) * aSamplesToRead * mChannels);
    mParent->requestMoreDataIfNeeded();
    return 0;
  }

  // Pull interleaved samples from the circular buffer and de-interleave them
  // into the planar layout the SoLoud mixer expects (each channel is a
  // contiguous block of aBufferSize frames).
  std::vector<float> temp(samplesToRead);
  const size_t readSamples =
      mParent->mCircularBuffer.read(temp.data(), samplesToRead);
  const size_t readFrames = readSamples / mChannels;
  if (readFrames > 0) {
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      for (size_t i = 0; i < readFrames; ++i) {
        aBuffer[ch * aBufferSize + i] = temp[i * mChannels + ch];
      }
    }
  }
  mParent->mDecodedSamplesRead += readSamples;

  // Zero any frames that were not filled.
  for (unsigned int ch = 0; ch < mChannels; ++ch) {
    std::memset(aBuffer + ch * aBufferSize + readFrames, 0,
                sizeof(float) * (aSamplesToRead - readFrames));
  }

  // Do not call checkBuffering from the audio thread: getPause/getPosition
  // acquire the Soloud mixer lock and would deadlock with the thread that
  // is currently calling getAudio(). Buffering checks are done when the user
  // adds data or marks the end of the stream (main thread).
  mParent->requestMoreDataIfNeeded();
  return readFrames;
}

result PullBufferStreamInstance::seek(double aSeconds, float *aScratch,
                                      unsigned int aScratchSize) {
  if (mParent == nullptr || !mParent->isValid()) {
    return INVALID_PARAMETER;
  }

  // Clamp to the beginning; negative seeks are treated as 0.
  if (aSeconds <= 0.0) {
    rewind();
    return SO_NO_ERROR;
  }

  const double totalLength = mParent->getLength();
  if (totalLength > 0.0 && aSeconds > totalLength) {
    aSeconds = totalLength;
  }

  const size_t targetSample = static_cast<size_t>(
      std::floor(aSeconds * mParent->mPCMformat.sampleRate *
                 mParent->mPCMformat.channels));

  bool didInBufferSeek = false;
  {
    std::lock_guard<std::mutex> lock(mParent->mDecodeMutex);

    // Move decoded samples that did not fit earlier into the circular buffer so
    // they become part of the seekable range.
    mParent->drainBacklogLocked();

    // The visual red bar is the circular-buffer window:
    // [read + available - capacity, read + available]. As the playhead advances,
    // read grows and available shrinks, so the window stays steady until new
    // data is decoded. Seeking anywhere inside this window should be in-buffer.
    const uint64_t bufferedEnd =
        mParent->mDecodedSamplesRead + mParent->mCircularBuffer.available();
    const uint64_t bufferedStart = bufferedEnd > mParent->mCircularBuffer.capacity()
        ? bufferedEnd - mParent->mCircularBuffer.capacity()
        : 0;

    // mBufferStartTime marks the logical start of valid decoded data (set by
    // out-of-buffer seeks). The physical circular buffer can still hold older
    // samples before this point, so clamp the seek window to the logical start
    // to prevent seeking into stale decoded audio.
    const uint64_t logicalStartSamples = mParent->mBufferStartTime > 0.0
        ? static_cast<uint64_t>(std::floor(mParent->mBufferStartTime *
                                           mParent->mPCMformat.sampleRate *
                                           mParent->mPCMformat.channels))
        : 0;
    const uint64_t effectiveStart = std::max(bufferedStart, logicalStartSamples);

    if (targetSample >= effectiveStart && targetSample <= bufferedEnd) {
      if (targetSample >= mParent->mDecodedSamplesRead) {
        // Forward in-buffer seek: advance the read pointer to the target.
        const size_t advance = targetSample - mParent->mDecodedSamplesRead;
        const size_t before = mParent->mCircularBuffer.available();
        mParent->mCircularBuffer.advanceRead(advance);
        const size_t after = mParent->mCircularBuffer.available();
        mParent->mDecodedSamplesRead += before - after;
      } else {
        // Backward in-buffer seek: restore samples that are still physically
        // in the circular buffer. rewindRead() enforces the safe rewind limit
        // (before the first wrap it will not cross into uninitialized data).
        const size_t desiredRewind =
            mParent->mDecodedSamplesRead - targetSample;
        const size_t rewound = mParent->mCircularBuffer.rewindRead(desiredRewind);
        if (rewound == desiredRewind) {
          mParent->mDecodedSamplesRead = targetSample;
        } else {
          // Not enough data is still physically present; fall through to the
          // out-of-buffer seek path.
        }
      }
      if (mParent->mDecodedSamplesRead == targetSample) {
        mStreamPosition = aSeconds;
        mParent->mWaitingForData.store(false);
        didInBufferSeek = true;
      }
    }
  }

  if (didInBufferSeek) {
    mParent->requestMoreDataIfNeeded();
    return SO_NO_ERROR;
  }

  // Out-of-buffer seek: move the play head to the target, request data from
  // the encoded byte offset, and mark a pending seek.
  const uint64_t newOffset = mParent->timeToEncodedByteOffset(aSeconds);

  if (newOffset == 0) {
    // The decoder does not yet know how to map this time to an encoded byte
    // offset. Keep the current play head and buffer, request more data, and
    // let applyPendingSeek retry once the mapping is available.
    mParent->mPendingSeek.store(true);
    mParent->mPendingSeekTime = aSeconds;
    mParent->mPendingSeekByteOffset = 0;
    mParent->requestMoreDataIfNeeded();
    return SO_NO_ERROR;
  }

  // Discard the currently buffered decoded range without zeroing the
  // underlying memory, matching the circular-buffer plan.
  mParent->resetBufferWindow(targetSample, targetSample, aSeconds, newOffset,
                             newOffset);
  mStreamPosition = aSeconds;
  mParent->mPendingSeek.store(true);
  mParent->mPendingSeekTime = aSeconds;
  mParent->mPendingSeekByteOffset = newOffset;
  mParent->mDataIsEnded.store(false);
  mParent->mWaitingForData.store(false);
  mParent->resetDecoderForSeek(targetSample);
  mParent->applyPendingSeek();
  return SO_NO_ERROR;
}

result PullBufferStreamInstance::rewind() {
  if (mParent == nullptr || !mParent->isValid()) {
    return INVALID_PARAMETER;
  }
  // Rewind: discard the currently buffered decoded range without zeroing the
  // underlying memory so the circular buffer can continue from the start.
  mParent->resetBufferWindow(0, 0, 0.0, 0, 0);
  mStreamPosition = 0.0;
  mParent->mPendingSeek.store(false);
  mParent->mPendingSeekTime = 0.0;
  mParent->mPendingSeekByteOffset = 0;
  if (mParent->mPCMformat.dataType == BufferType::AUTO &&
      mParent->mStreamDecoder) {
    mParent->mStreamDecoder = std::make_unique<StreamDecoder>();
    mParent->mStreamDecoder->setTotalAudioSizeBytes(mParent->mAudioSizeBytes);
  }
  mParent->mWaitingForData.store(false);
  mParent->requestMoreDataIfNeeded();
  return SO_NO_ERROR;
}

bool PullBufferStreamInstance::hasEnded() {
  if (mParent == nullptr || !mParent->isValid()) {
    return true;
  }
  return mParent->isDataEnded() && mParent->mCircularBuffer.empty() &&
         mParent->mDecodedBacklog.empty();
}

// /////////////////////////////////////////////////////////////////////////////
// PullBufferStream
// /////////////////////////////////////////////////////////////////////////////

PullBufferStream::PullBufferStream() = default;

PullBufferStream::~PullBufferStream() = default;

void PullBufferStream::resetBufferWindow(uint64_t decodedSamplesRead,
                                         uint64_t decodedSamplesWritten,
                                         double bufferStartTime,
                                         uint64_t totalReceivedBytes,
                                         uint64_t sequentialReadOffset) {
  mCircularBuffer.advanceRead(mCircularBuffer.available());
  mEncodedBuffer.clear();
  mDecodedBacklog.clear();
  mDecodedSamplesRead = decodedSamplesRead;
  mDecodedSamplesWritten = decodedSamplesWritten;
  mBufferStartTime = bufferStartTime;
  mTotalReceivedBytes = totalReceivedBytes;
  mSequentialReadOffset = sequentialReadOffset;
}

void PullBufferStream::resetProbeState() {
  mProbedDuration = 0.0;
  mTailProbeChunkSeen = false;
  mDurationProbeTailRangeStart = 0;
  mDurationProbeTailRangeEnd = 0;
  mNextTailOffset = 0;
  mSeekTableBuildChunks = 0;
  mTailProbeBuffer.clear();
}

void PullBufferStream::resetDecoderForSeek(uint64_t targetSample) {
  if (mPCMformat.dataType != BufferType::AUTO || !mStreamDecoder) return;
  const DetectedType type = mStreamDecoder->getWrapperType();
  if (type == DetectedType::BUFFER_OGG_OPUS ||
      type == DetectedType::BUFFER_OGG_VORBIS ||
      type == DetectedType::BUFFER_WAV ||
      type == DetectedType::BUFFER_FLAC) {
    mStreamDecoder->prepareForSeek(targetSample);
  } else {
    mStreamDecoder = std::make_unique<StreamDecoder>();
  }
}

PlayerErrors PullBufferStream::setPullBufferStream(
    Player *aPlayer, ActiveSound *aParent,
    unsigned int bufferSizeBytes, double bufferTriggerPosition,
    unsigned int sampleRate, unsigned int channels, BufferType format,
    uint64_t audioSizeBytes,
    dartOnBufferingCallback_t onBufferingCallback,
    dartOnMetadataCallback_t onMetadataCallback,
    dartOnMoreDataIsNeededCallback_t onMoreDataIsNeededCallback,
    dartOnAudioDurationCallback_t onAudioDurationCallback) {
  if (aPlayer == nullptr || aParent == nullptr) {
    return PlayerErrors::nullPointer;
  }
  if (bufferSizeBytes == 0 || bufferTriggerPosition < 0.0 ||
      bufferTriggerPosition > 1.0) {
    return PlayerErrors::invalidParameter;
  }
  if (sampleRate == 0 || channels == 0) {
    return PlayerErrors::invalidParameter;
  }
  if (audioSizeBytes == 0) {
    return PlayerErrors::invalidParameter;
  }
  if (format == BufferType::OPUS) {
    format = BufferType::AUTO;
  }

  mThePlayer = aPlayer;
  mParent = aParent;
  mBufferTriggerPosition = bufferTriggerPosition;
  mAudioSizeBytes = audioSizeBytes;
  mPCMformat = {sampleRate, channels, sizeof(float), format};
  mTotalReceivedBytes = 0;
  mSequentialReadOffset = 0;
  mDecodedSamplesRead = 0;
  mDecodedSamplesWritten = 0;
  mBufferStartTime = 0.0;
  mDataIsEnded.store(false);
  mIsBuffering.store(false);
  mWaitingForData.store(false);
  mMetadataReceived.store(false);
  mPendingSeek.store(false);
  mPendingSeekTime = 0.0;
  mPendingSeekByteOffset = 0;
  mOnBufferingCallback.store(onBufferingCallback);
  mOnMetadataCallback.store(onMetadataCallback);
  mOnMoreDataIsNeededCallback.store(onMoreDataIsNeededCallback);
  mOnAudioDurationCallback.store(onAudioDurationCallback);
  mDurationProbeState = DurationProbeState::None;
  resetProbeState();

  // Align buffer size to a whole number of float frames.
  const size_t frameSize = channels * sizeof(float);
  size_t capacitySamples = bufferSizeBytes / sizeof(float);
  if (frameSize > 0) {
    capacitySamples = (bufferSizeBytes / frameSize) * channels;
  }
  mCircularBuffer.setCapacity(capacitySamples);

  mEncodedBuffer.clear();
  mDecodeScratch.clear();

  if (format == BufferType::AUTO) {
    mStreamDecoder = std::make_unique<StreamDecoder>();
    mStreamDecoder->setTotalAudioSizeBytes(mAudioSizeBytes);
    mDurationProbeState = DurationProbeState::Probing;
  } else {
    // PCM formats: duration is known from the declared size and format.
    mStreamDecoder.reset();
    const double bytesPerSecond = static_cast<double>(sampleRate) *
                                  static_cast<double>(channels) *
                                  static_cast<double>(mPCMformat.bytesPerSample);
    if (bytesPerSecond > 0.0) {
      mProbedDuration = static_cast<double>(audioSizeBytes) / bytesPerSecond;
    }
    mDurationProbeState = DurationProbeState::Done;
    finishDurationProbe(mProbedDuration);
  }

// On the web the Dart callback is stored on globalThis after the WASM call
// returns, so we cannot request data from inside the C++ call. The web binding
// triggers the initial request manually once the callback is registered.
#ifndef __EMSCRIPTEN__
  // Immediately request the first chunk so the caller can start loading data.
  requestMoreDataIfNeeded();
#endif

  return PlayerErrors::noError;
}

void PullBufferStream::resetPullBufferStream() {
  mCircularBuffer.clear();
  mEncodedBuffer.clear();
  mDecodeScratch.clear();
  mDecodedBacklog.clear();
  mTotalReceivedBytes = 0;
  mSequentialReadOffset = 0;
  mDecodedSamplesRead = 0;
  mDecodedSamplesWritten = 0;
  mBufferStartTime = 0.0;
  mDataIsEnded.store(false);
  mIsBuffering.store(false);
  mWaitingForData.store(false);
  mMetadataReceived.store(false);
  mPendingSeek.store(false);
  mPendingSeekTime = 0.0;
  mPendingSeekByteOffset = 0;
  resetProbeState();
  if (mPCMformat.dataType == BufferType::AUTO) {
    mStreamDecoder = std::make_unique<StreamDecoder>();
    mStreamDecoder->setTotalAudioSizeBytes(mAudioSizeBytes);
    mDurationProbeState = DurationProbeState::Probing;
  } else {
    mDurationProbeState = DurationProbeState::Done;
  }
  requestMoreDataIfNeeded();
}

void PullBufferStream::setDataIsEnded() {
  if (mStreamDecoder) {
    mStreamDecoder->setDataEnded();
  }
  // Decode any remaining encoded data. Some formats need several passes (or
  // all data to be present) before they can produce decoded samples.
  size_t lastCircularSize = 0;
  size_t lastBacklogSize = 0;
  do {
    lastCircularSize = mCircularBuffer.available();
    lastBacklogSize = mDecodedBacklog.size();
    decodePendingData();
  } while (mCircularBuffer.available() != lastCircularSize ||
           mDecodedBacklog.size() != lastBacklogSize);
  mDataIsEnded.store(true);

  // If the decoder now knows the duration (e.g. small Ogg files), report it.
  if (mDurationProbeState != DurationProbeState::Done && mStreamDecoder &&
      mStreamDecoder->getDuration() > 0.0) {
    finishDurationProbe(mStreamDecoder->getDuration());
  }

  checkBuffering();
}

double PullBufferStream::getBufferStartTime() const {
  const unsigned int sampleRate = mPCMformat.sampleRate;
  const unsigned int channels = mPCMformat.channels;
  if (sampleRate == 0 || channels == 0) return 0.0;

  // The decoded window is the actual contents of the circular buffer. As the
  // playhead consumes samples, mDecodedSamplesRead advances and
  // mCircularBuffer.available() shrinks by the same amount, so their sum (the
  // decoded end) stays constant until new data is loaded. The red bar drawn
  // from [startTime, endTime] therefore stays steady and the playhead moves
  // inside it until the trigger position requests more data.
  const double endTime = getBufferEndTime();
  const double startTimeFromCapacity =
      endTime - static_cast<double>(mCircularBuffer.capacity()) /
                    static_cast<double>(sampleRate * channels);

  // Out-of-buffer seeks set mBufferStartTime to the logical start of the valid
  // decoded window. Clamp the computed start to it so the red bar never shows
  // stale decoded audio to the left of the playhead.
  // During normal playback the sliding capacity-based window is used from the
  // start, so the red bar stays steady as the playhead advances.
  double startTime = startTimeFromCapacity;
  if (mBufferStartTime > 0.0) {
    startTime = std::max(startTime, mBufferStartTime);
  }

  const double totalLength = getLength();
  if (totalLength > 0.0) {
    return std::max(0.0, std::min(startTime, totalLength));
  }
  return std::max(0.0, startTime);
}

double PullBufferStream::getBufferEndTime() const {
  const unsigned int sampleRate = mPCMformat.sampleRate;
  const unsigned int channels = mPCMformat.channels;
  if (sampleRate == 0 || channels == 0) return 0.0;
  const double endTime = static_cast<double>(mDecodedSamplesRead +
                                              mCircularBuffer.available()) /
                         static_cast<double>(sampleRate * channels);
  const double totalLength = getLength();
  if (totalLength > 0.0) {
    return std::min(endTime, totalLength);
  }
  return endTime;
}

PlayerErrors PullBufferStream::addAudioData(const void *aData,
                                            unsigned int aDataLen,
                                            uint64_t aOffset) {
  if (mDataIsEnded.load()) {
    return PlayerErrors::streamEndedAlready;
  }
  if (aDataLen == 0) {
    decodePendingData();
    return PlayerErrors::noError;
  }

  const bool isSequential = (aOffset == 0 || aOffset == mSequentialReadOffset);

  // Once the duration has been found, discard late tail chunks that were
  // already in flight when the probe finished. They are not needed for
  // playback and would otherwise be fed to the main decoder out of order.
  // Sequential data that happens to fall in the tail range (e.g. after a
  // seek or when normal playback reaches the end of the file) must still be
  // accepted.
  if (mDurationProbeState == DurationProbeState::Done &&
      aOffset >= mDurationProbeTailRangeStart.load() &&
      aOffset < mDurationProbeTailRangeEnd.load() && !isSequential) {
    mWaitingForData.store(false);
    requestMoreDataIfNeeded();
    return PlayerErrors::noError;
  }

  uint64_t effectiveOffset = aOffset;
  if (aOffset == 0) {
    effectiveOffset = mSequentialReadOffset;
  }

  // If this chunk falls inside the requested Ogg tail-probe range, do not
  // store it in the circular buffer; accumulate it in a temporary probe
  // buffer and discard it once the duration is known.
  if (mDurationProbeState == DurationProbeState::TailRequested &&
      aOffset >= mDurationProbeTailRangeStart.load() &&
      aOffset < mDurationProbeTailRangeEnd.load()) {
    {
      std::lock_guard<std::mutex> lock(mDecodeMutex);
      const auto *bytes = static_cast<const unsigned char *>(aData);
      mTailProbeBuffer.insert(mTailProbeBuffer.end(), bytes, bytes + aDataLen);
      mTotalReceivedBytes += aDataLen;
    }
    if (aOffset == mNextTailOffset.load()) {
      mNextTailOffset.store(aOffset + aDataLen);
    }
    soloud_platform_log(
        "[PullBuffer] addAudioData tail probe len=%u offset=%llu "
        "nextTailOffset=%llu\n",
        aDataLen, static_cast<unsigned long long>(aOffset),
        static_cast<unsigned long long>(mNextTailOffset));
    probeDurationFromOggTail(aOffset);
    mWaitingForData.store(false);
    requestMoreDataIfNeeded();
    return PlayerErrors::noError;
  }

  {
    std::lock_guard<std::mutex> lock(mDecodeMutex);
    const auto *bytes = static_cast<const unsigned char *>(aData);
    mEncodedBuffer.insert(mEncodedBuffer.end(), bytes, bytes + aDataLen);
    mTotalReceivedBytes += aDataLen;
  }
  if (isSequential && effectiveOffset == mSequentialReadOffset) {
    mSequentialReadOffset += aDataLen;
  }
  if (mDurationProbeState == DurationProbeState::BuildingSeekTable) {
    ++mSeekTableBuildChunks;
  }

  // If the sequential feed has reached the declared end of the file, mark
  // the stream as ended so the decoder can flush its final frames and the
  // voice can finish cleanly. Callers no longer need to explicitly call
  // setDataIsEnded once the total audioSizeBytes is known.
  if (mSequentialReadOffset >= mAudioSizeBytes) {
    setDataIsEnded();
  }

  decodePendingData();

  // After decoding the first sequential chunk, try to determine the total
  // duration from the format header for formats that provide it.
  if (mDurationProbeState == DurationProbeState::Probing && isSequential) {
    probeDurationFromFirstChunk();
  }

  checkBuffering();
  // New data has arrived, so we are no longer waiting for the previous
  // request. Reset the flag before checking whether another request is needed
  // to avoid duplicate callbacks with the same offset.
  mWaitingForData.store(false);
  requestMoreDataIfNeeded();
  return PlayerErrors::noError;
}

void PullBufferStream::getBufferTimeRange(double &startTime, double &endTime) const {
  startTime = getBufferStartTime();
  endTime = getBufferEndTime();
}

void PullBufferStream::probeDurationFromFirstChunk() {
  if (mDurationProbeState != DurationProbeState::Probing) return;
  if (!mStreamDecoder) return;

  const DetectedType type = mStreamDecoder->getWrapperType();
  switch (type) {
  case DetectedType::BUFFER_FLAC:
  case DetectedType::BUFFER_OGG_FLAC:
  case DetectedType::BUFFER_WAV:
  case DetectedType::BUFFER_MP3_WITH_ID3:
  case DetectedType::BUFFER_MP3_STREAM: {
    const double duration = mStreamDecoder->getDuration();
    if (duration > 0.0) {
      finishDurationProbe(duration);
    }
    break;
  }
  case DetectedType::BUFFER_OGG_VORBIS:
  case DetectedType::BUFFER_OGG_OPUS: {
    // Ogg durations are not in the header; request the last 64 KB of the
    // file and read the granule position of the final Ogg page.
    // For files that fit in 64 KB, do not switch to tail probing; the
    // duration will be reported once the whole stream is decoded.
    if (mAudioSizeBytes > 65536) {
      const uint64_t tailOffset = mAudioSizeBytes - 65536;
      mDurationProbeTailRangeStart.store(tailOffset);
      mDurationProbeTailRangeEnd.store(mAudioSizeBytes);
      mNextTailOffset.store(tailOffset);
      mSeekTableBuildChunks = 0;
      mDurationProbeState = DurationProbeState::BuildingSeekTable;
      requestMoreDataIfNeeded();
    }
    break;
  }
  default:
    // Format not yet detected or unsupported; wait for more data.
    break;
  }
}

#if !defined(NO_XIPH_LIBS)
/// Extract the total duration from the final (EOS) Ogg page contained in a tail
/// chunk. Returns a negative value if the EOS page is not yet present.
static double extractOggDurationFromTail(const std::vector<unsigned char> &tail,
                                         int sampleRate, int preSkip) {
  if (tail.empty() || sampleRate <= 0) return -1.0;
  ogg_sync_state oy;
  ogg_page og;
  ogg_sync_init(&oy);
  ogg_int64_t lastGranule = -1;
  size_t bytesWritten = 0;
  const unsigned char *data = tail.data();
  const size_t size = tail.size();
  int ret;
  while (bytesWritten < size) {
    char *buffer = ogg_sync_buffer(&oy, size - bytesWritten);
    const size_t toWrite = size - bytesWritten;
    memcpy(buffer, data + bytesWritten, toWrite);
    ogg_sync_wrote(&oy, toWrite);
    bytesWritten += toWrite;
    while ((ret = ogg_sync_pageout(&oy, &og)) != 0) {
      if (ret == 1) {
        const ogg_int64_t granule = ogg_page_granulepos(&og);
        if (granule >= 0) {
          lastGranule = granule;
        }
        if (ogg_page_eos(&og)) {
          ogg_sync_clear(&oy);
          if (lastGranule < 0) return -1.0;
          const ogg_int64_t samples = lastGranule - preSkip;
          if (samples <= 0) return 0.0;
          return static_cast<double>(samples) /
                 static_cast<double>(sampleRate);
        }
      }
      // ret == -1 indicates a sync loss in the middle of the tail data (the
      // tail can start inside a page); keep calling pageout to resync.
    }
  }
  ogg_sync_clear(&oy);
  return -1.0;
}
#endif

void PullBufferStream::probeDurationFromOggTail(uint64_t aOffset) {
  (void)aOffset;
#if !defined(NO_XIPH_LIBS)
  if (mDurationProbeState != DurationProbeState::TailRequested) return;
  if (mTailProbeBuffer.empty()) return;

  // Use the decoder wrapper to obtain the correct granule sample rate and
  // any header pre-skip (Opus). Fall back to the decoded PCM sample rate if
  // the wrapper does not provide a granule rate.
  int sampleRate = 0;
  int preSkip = 0;
  if (mStreamDecoder) {
    sampleRate = mStreamDecoder->granuleSampleRate();
    preSkip = mStreamDecoder->preSkip();
  }
  if (sampleRate <= 0) {
    sampleRate = static_cast<int>(mPCMformat.sampleRate);
  }

  const double duration =
      extractOggDurationFromTail(mTailProbeBuffer, sampleRate, preSkip);
  if (duration >= 0.0) {
    mTailProbeBuffer.clear();
    mTailProbeChunkSeen = true;
    finishDurationProbe(duration);
  }
#else
  // Ogg support is not available in this build; leave the duration unknown.
  mDurationProbeState = DurationProbeState::Done;
#endif
}

void PullBufferStream::finishDurationProbe(double duration) {
  mProbedDuration = duration;
  mDurationProbeState = DurationProbeState::Done;
  callOnAudioDurationCallback(duration);
}

void PullBufferStream::decodePendingDataLocked() {
  // First, move any previously decoded samples that did not fit into the
  // circular buffer. If the circular buffer is still full, we cannot accept
  // any newly decoded samples either, so stop here.
  drainBacklogLocked();
  if (!mDecodedBacklog.empty()) {
    soloud_platform_log(
        "[PullBuffer] decodePendingData: buffer full, backlog=%zu\n",
        mDecodedBacklog.size());
    return;
  }

  if (mPCMformat.dataType == BufferType::AUTO) {
    if (!mStreamDecoder) return;

    // dr_mp3 can fail to decode the next frame when only a tiny chunk of
    // encoded data is available, especially with 1 KB input chunks. Wait until
    // we have at least a few kilobytes (or the stream has ended) before asking
    // the decoder to produce more frames.
    constexpr size_t kMinEncodedBufferForDecode = 16 * 1024;
    if (mEncodedBuffer.size() < kMinEncodedBufferForDecode && !mDataIsEnded.load()) {
      return;
    }

    int sampleRate = mThePlayer->mSampleRate;
    int channels = mThePlayer->mChannels;
    const size_t maxAllowedSamples =
        mDecodedSamplesRead + mCircularBuffer.capacity() > mDecodedSamplesWritten
            ? (mDecodedSamplesRead + mCircularBuffer.capacity()) - mDecodedSamplesWritten
            : 0;
    if (maxAllowedSamples == 0) {
      return;
    }
    auto [decoded, error] = mStreamDecoder->decode(
        mEncodedBuffer, &sampleRate, &channels, [&](AudioMetadata meta) {
          if (this->mOnMetadataCallback != nullptr)
            this->callOnMetadataCallback(meta);
        }, maxAllowedSamples);

    switch (error) {
    case DecoderError::FormatNotSupported:
      return; // Caller will continue to retry or fail eventually.
    case DecoderError::NoXiphLibs:
      return;
    case DecoderError::FailedToCreateDecoder:
      return;
    case DecoderError::ErrorReadingOggOpusPage:
      return;
    default:
      break;
    }

    if (!decoded.empty()) {
      if (!mMetadataReceived.load()) {
        if (sampleRate != -1) {
          mPCMformat.sampleRate = static_cast<unsigned int>(sampleRate);
          mBaseSamplerate = static_cast<float>(sampleRate);
        }
        if (channels != -1) {
          mPCMformat.channels = static_cast<unsigned int>(channels);
          mChannels = static_cast<unsigned int>(channels);
        }
        mMetadataReceived.store(true);
      }
      // Account for every sample the decoder produced, even if the circular
      // buffer cannot hold them all right now.
      mDecodedSamplesWritten += decoded.size();
      size_t written = mCircularBuffer.write(decoded.data(), decoded.size());
      if (written < decoded.size()) {
        mDecodedBacklog.insert(mDecodedBacklog.end(),
                               decoded.data() + written,
                               decoded.data() + decoded.size());
      }
      // soloud_platform_log(
      //     "[PullBuffer] decodePendingData decoded=%zu written=%zu backlog=%zu\n",
      //     decoded.size(), written, mDecodedBacklog.size());
      // Once decoded, the encoded data is consumed from the pending buffer.
      mEncodedBuffer.clear();
      applyPendingSeek();

      // For formats whose duration becomes known only after decoding (e.g.
      // small Ogg files once the EOS page is seen), report it now.
      if (mDurationProbeState == DurationProbeState::Probing &&
          mStreamDecoder && mStreamDecoder->getDuration() > 0.0) {
        finishDurationProbe(mStreamDecoder->getDuration());
      }
    }
  } else {
    // Raw PCM: convert to float and write into the circular buffer.
    const size_t frameSize = mPCMformat.bytesPerSample * mPCMformat.channels;
    const size_t maxAllowedSamples =
        mDecodedSamplesRead + mCircularBuffer.capacity() > mDecodedSamplesWritten
            ? (mDecodedSamplesRead + mCircularBuffer.capacity()) - mDecodedSamplesWritten
            : 0;
    if (maxAllowedSamples == 0) return;
    const size_t framesToProcess = std::min(
        mEncodedBuffer.size() / frameSize,
        maxAllowedSamples / mPCMformat.channels);
    if (framesToProcess == 0) return;

    const size_t samples = framesToProcess * mPCMformat.channels;
    mDecodeScratch.resize(samples * sizeof(float));
    float *floatData = reinterpret_cast<float *>(mDecodeScratch.data());

    const auto *src = mEncodedBuffer.data();
    switch (mPCMformat.dataType) {
    case BufferType::PCM_F32LE:
      std::memcpy(floatData, src, samples * sizeof(float));
      break;
    case BufferType::PCM_S8:
      for (size_t i = 0; i < samples; ++i) {
        floatData[i] = static_cast<float>(src[i]) / 128.0f;
      }
      break;
    case BufferType::PCM_S16LE: {
      const auto *src16 = reinterpret_cast<const int16_t *>(src);
      for (size_t i = 0; i < samples; ++i) {
        floatData[i] = static_cast<float>(src16[i]) / 32768.0f;
      }
      break;
    }
    case BufferType::PCM_S32LE: {
      const auto *src32 = reinterpret_cast<const int32_t *>(src);
      for (size_t i = 0; i < samples; ++i) {
        floatData[i] = static_cast<float>(src32[i]) / 2147483648.0f;
      }
      break;
    }
    default:
      break;
    }

    // Account for every sample that was converted, even if the circular
    // buffer cannot hold them all right now.
    mDecodedSamplesWritten += samples;
    size_t written = mCircularBuffer.write(floatData, samples);
    if (written < samples) {
      mDecodedBacklog.insert(mDecodedBacklog.end(), floatData + written,
                             floatData + samples);
    }
    mEncodedBuffer.erase(mEncodedBuffer.begin(),
                         mEncodedBuffer.begin() + framesToProcess * frameSize);
    mMetadataReceived.store(true);
    soloud_platform_log(
        "[PullBuffer] decodePendingData PCM frames=%zu written=%zu backlog=%zu\n",
        framesToProcess, written, mDecodedBacklog.size());
    applyPendingSeek();
  }
}

void PullBufferStream::decodePendingData() {
  std::lock_guard<std::mutex> lock(mDecodeMutex);
  decodePendingDataLocked();
}

void PullBufferStream::tryDecodePendingData() {
  std::unique_lock<std::mutex> lock(mDecodeMutex, std::try_to_lock);
  if (!lock.owns_lock()) return;
  decodePendingDataLocked();
}

void PullBufferStream::applyPendingSeek() {
  if (!mPendingSeek.load()) return;

  // If we haven't resolved the encoded byte offset yet, try again now that
  // more data may have arrived.
  if (mPendingSeekByteOffset == 0) {
    const uint64_t newOffset = timeToEncodedByteOffset(mPendingSeekTime);
    if (newOffset == 0) {
      // Still unknown; the caller will request more data to build the table.
      return;
    }

    // The offset is now known. Move the play head to the target and reset the
    // decoder so that the next incoming data is decoded from the new offset.
    const uint64_t targetSample = static_cast<uint64_t>(
        std::floor(mPendingSeekTime * mPCMformat.sampleRate *
                   mPCMformat.channels));
    resetBufferWindow(targetSample, targetSample, mPendingSeekTime, newOffset,
                      newOffset);
    if (mInstance != nullptr) {
      mInstance->mStreamPosition = mPendingSeekTime;
    }
    resetDecoderForSeek(targetSample);
    mPendingSeekByteOffset = newOffset;
    mDataIsEnded.store(false);
    mWaitingForData.store(false);
  }

  if (mCircularBuffer.available() == 0) {
    // Need fresh data at the requested offset before the seek can be satisfied.
    return;
  }

  mPendingSeek.store(false);
  // The play head was already moved by seek() or by the offset-resolution block
  // above; now that data is here we can resume normal playback.
  checkBuffering();
}

void PullBufferStream::drainBacklog() {
  // The audio thread calls this while the SoLoud mixer lock is held. The main
  // thread may hold the decode mutex while calling back into the player (e.g.
  // checkBuffering() -> setPause()). Use a try-lock to avoid a lock-order
  // inversion deadlock; if the main thread is busy, the backlog will be drained
  // on the next audio callback.
  std::unique_lock<std::mutex> lock(mDecodeMutex, std::try_to_lock);
  if (!lock.owns_lock()) return;
  drainBacklogLocked();
}

void PullBufferStream::drainBacklogLocked() {
  if (mDecodedBacklog.empty()) return;

  size_t written = mCircularBuffer.write(mDecodedBacklog.data(),
                                          mDecodedBacklog.size());
  if (written > 0) {
    mDecodedBacklog.erase(mDecodedBacklog.begin(),
                          mDecodedBacklog.begin() + written);
  }
}

void PullBufferStream::requestMoreDataIfNeeded() {
  // soloud_platform_log(
  //     "[PullBuffer] requestMoreDataIfNeeded enter callback=%s dataIsEnded=%d waitingForData=%d probeState=%d availablePlusBacklog=%zu aheadThresholdSamples=%zu\n",
  //     mOnMoreDataIsNeededCallback.load() == nullptr ? "null" : "set",
  //     mDataIsEnded.load() ? 1 : 0,
  //     mWaitingForData.load() ? 1 : 0,
  //     static_cast<int>(mDurationProbeState),
  //     mCircularBuffer.available() + mDecodedBacklog.size(),
  //     mBufferTriggerPosition / sizeof(float));

  auto callback = mOnMoreDataIsNeededCallback.load();
  if (callback == nullptr) return;
  if (mDataIsEnded.load()) return;
  if (mWaitingForData.load()) return;
  // The sequential feed has already reached the declared end of the file;
  // there is no more data to request.
  if (mSequentialReadOffset >= mAudioSizeBytes) return;

  // A pending seek has the highest priority so it can resolve quickly and
  // does not get stuck behind the duration-probe tail. Once the seek offset
  // is known, mSequentialReadOffset is already set to that offset, so
  // requesting it continues the stream from the seek point.
  if (mPendingSeek.load()) {
    const uint64_t offset = mSequentialReadOffset;
    mWaitingForData.store(true);
    soloud_platform_log(
        "[PullBuffer] requestMoreDataIfNeeded pending seek offset=%llu\n",
        static_cast<unsigned long long>(offset));
    callOnMoreDataIsNeededCallback(offset);
    return;
  }

  // If we are waiting for the Ogg tail-probe chunk, request the next byte
  // within the tail range until the whole tail has been fetched or the
  // duration has been determined.
  if (mDurationProbeState == DurationProbeState::BuildingSeekTable) {
    const bool seekTableReady =
        mStreamDecoder && mStreamDecoder->canSeekToTime(1.0);
    const bool decoderBlocked = mDecodedBacklog.size() > 0;
    if (seekTableReady || mSeekTableBuildChunks >= kMaxSeekTableBuildChunks ||
        decoderBlocked) {
      mDurationProbeState = DurationProbeState::TailRequested;
      mSeekTableBuildChunks = 0;
      requestMoreDataIfNeeded();
      return;
    }
    mWaitingForData.store(true);
    const uint64_t offset = mSequentialReadOffset;
    soloud_platform_log(
        "[PullBuffer] requestMoreDataIfNeeded build seek table offset=%llu\n",
        static_cast<unsigned long long>(offset));
    callOnMoreDataIsNeededCallback(offset);
    return;
  }

  if (mDurationProbeState == DurationProbeState::TailRequested) {
    const size_t available = mCircularBuffer.available();
    const size_t backlog = mDecodedBacklog.size();
    const size_t availablePlusBacklog = available + backlog;
    const size_t capacity = mCircularBuffer.capacity();
    if (capacity == 0) return;
    const double fraction = std::clamp(mBufferTriggerPosition, 0.0, 1.0);
    const size_t aheadThresholdSamples =
        static_cast<size_t>((1.0 - fraction) * capacity);

    soloud_platform_log(
        "[PullBuffer] requestMoreDataIfNeeded TailRequested availablePlusBacklog=%zu aheadThresholdSamples=%zu aboveThreshold=%s\n",
        availablePlusBacklog, aheadThresholdSamples,
        availablePlusBacklog > aheadThresholdSamples ? "yes" : "no");

    // Keep playback fed while probing the tail. If the decoded buffer is below
    // the ahead threshold, fall through to the normal sequential request.
    if (availablePlusBacklog > aheadThresholdSamples) {
      if (mNextTailOffset.load() >= mDurationProbeTailRangeEnd.load()) {
        return;
      }
      mWaitingForData.store(true);
      const uint64_t offset = mNextTailOffset.load();
      soloud_platform_log(
          "[PullBuffer] requestMoreDataIfNeeded tail offset=%llu\n",
          static_cast<unsigned long long>(offset));
      callOnMoreDataIsNeededCallback(offset);
      return;
    }
    soloud_platform_log(
        "[PullBuffer] requestMoreDataIfNeeded TailRequested below threshold, falling through to sequential request\n");
  }

  const size_t available = mCircularBuffer.available();
  const size_t backlog = mDecodedBacklog.size();
  const size_t availablePlusBacklog = available + backlog;
  const size_t capacity = mCircularBuffer.capacity();
  if (capacity == 0) return;

  // If the available data is below the ahead threshold, request more.
  // Include the backlog so decoded-but-not-yet-buffered samples count toward
  // the target and we do not get stuck when the circular buffer is tiny.
  const double fraction = std::clamp(mBufferTriggerPosition, 0.0, 1.0);
  const size_t aheadThresholdSamples =
      static_cast<size_t>((1.0 - fraction) * capacity);
  if (availablePlusBacklog > aheadThresholdSamples) {
    return;
  }

  mWaitingForData.store(true);
  const uint64_t offset = mSequentialReadOffset;
  soloud_platform_log(
      "[PullBuffer] requestMoreDataIfNeeded offset=%llu available=%zu capacity=%zu\n",
      static_cast<unsigned long long>(offset),
      mCircularBuffer.available(), mCircularBuffer.capacity());
  callOnMoreDataIsNeededCallback(offset);
}

void PullBufferStream::checkBuffering() {
  if (mParent == nullptr || mParent->handle.empty()) return;
  if (mBaseSamplerate == 0.0f || mPCMformat.channels == 0) return;

  const size_t availableSamples = mCircularBuffer.available();
  const size_t backlogSamples = mDecodedBacklog.size();
  const size_t availablePlusBacklog = availableSamples + backlogSamples;
  const double availableTime =
      static_cast<double>(availablePlusBacklog) /
      (static_cast<double>(mBaseSamplerate) *
       static_cast<double>(mPCMformat.channels));

  // Pause when the decoded audio is nearly exhausted; resume once we have a
  // comfortable amount ahead again. The backlog is counted as available so
  // tiny decoded buffers do not keep the stream paused indefinitely.
  // The thresholds are capped relative to the circular buffer capacity so
  // very small buffers can still reach the resume threshold and start playing.
  const double bufferCapacityTime =
      static_cast<double>(mCircularBuffer.capacity()) /
      (static_cast<double>(mBaseSamplerate) *
       static_cast<double>(mPCMformat.channels));
  const double kBufferingResumeTime = std::min(0.5, bufferCapacityTime * 0.8);
  const double kBufferingPauseTime = std::min(0.05, bufferCapacityTime * 0.1);


  SoLoud::handle handle = mParent->handle[0].handle;
  const bool isPaused = mThePlayer->getPause(handle);

  if (!mDataIsEnded.load() && availableTime < kBufferingPauseTime &&
      !isPaused &&
      // When the circular buffer is very small (e.g., 100 KB), decoded data may
      // be backlogged for a short time. Do not pause while we still have a
      // backlog that is being drained, otherwise playback never starts.
      backlogSamples == 0) {
    mThePlayer->setPause(handle, true, false);
    mIsBuffering.store(true);
    callOnBufferingCallback(true, handle, availableTime);
    requestMoreDataIfNeeded();
  } else if (availableTime >= kBufferingResumeTime && isPaused &&
             !mParent->handle[0].isUserPaused) {
    mThePlayer->setPause(handle, false, false);
    mIsBuffering.store(false);
    callOnBufferingCallback(false, handle, availableTime);
  }
}

void PullBufferStream::callOnBufferingCallback(bool isBuffering,
                                               unsigned int handle,
                                               double time) {
  auto callback = mOnBufferingCallback.load();
  if (callback == nullptr) return;
#ifdef __EMSCRIPTEN__
  EM_ASM(
      {
        var functionName = "dartOnBufferingCallback_" + $3;
        if (typeof window[functionName] === "function") {
          var buffering = $0 == 1 ? true : false;
          window[functionName](buffering, $1, $2);
        }
      },
      isBuffering, handle, time, mParent->soundHash);
#else
  callback(isBuffering, handle, time);
#endif
}

void PullBufferStream::callOnMetadataCallback(AudioMetadata &metadata) {
  auto callback = mOnMetadataCallback.load();
  if (callback == nullptr) return;

  AudioMetadataFFI ffi = convertMetadataToFFI(metadata);
#ifdef __EMSCRIPTEN__
  EM_ASM_(
      {
        var functionName = "dartOnMetadataCallback_" + $1;
        if (typeof window[functionName] === "function") {
          window[functionName]($0);
        }
      },
      &ffi, mParent->soundHash);
#else
  callback(ffi);
#endif
}

void PullBufferStream::callOnMoreDataIsNeededCallback(uint64_t offset) {
  auto callback = mOnMoreDataIsNeededCallback.load();
  if (callback == nullptr) return;

#ifdef __EMSCRIPTEN__
  EM_ASM_(
      {
        var functionName = "dartOnMoreDataIsNeededCallback_" + $1;
        if (typeof window[functionName] === "function") {
          // Pass the offset as a JS Number. On the web Dart `int` is a
          // double-precision float, so large offsets may lose precision, but
          // this avoids BigInt conversion errors from EM_ASM with uint64_t.
          window[functionName]($0);
        }
      },
      static_cast<double>(offset), mParent->soundHash);
#else
  callback(offset);
#endif
}

void PullBufferStream::callOnAudioDurationCallback(double duration) {
  auto callback = mOnAudioDurationCallback.load();
  if (callback == nullptr) return;

#ifdef __EMSCRIPTEN__
  EM_ASM_(
      {
        var functionName = "dartOnAudioDurationCallback_" + $1;
        if (typeof window[functionName] === "function") {
          window[functionName]($0);
        }
      },
      duration, mParent->soundHash);
#else
  callback(duration);
#endif
}

void PullBufferStream::clearDartCallbacks() {
  mOnBufferingCallback.store(nullptr);
  mOnMetadataCallback.store(nullptr);
  mOnMoreDataIsNeededCallback.store(nullptr);
  mOnAudioDurationCallback.store(nullptr);
}

AudioSourceInstance *PullBufferStream::createInstance() {
  return new PullBufferStreamInstance(this);
}

SoLoud::time PullBufferStream::getLength() const {
  if (mPCMformat.dataType == BufferType::AUTO ||
      mPCMformat.dataType == BufferType::OPUS) {
    // Use the probed duration once known; otherwise the length is unknown.
    return mProbedDuration;
  }
  if (mPCMformat.sampleRate == 0 || mPCMformat.channels == 0) return 0.0;
  return static_cast<double>(mAudioSizeBytes) /
         (static_cast<double>(mPCMformat.sampleRate) *
          static_cast<double>(mPCMformat.bytesPerSample) *
          static_cast<double>(mPCMformat.channels));
}

SoLoud::time PullBufferStream::getBufferedLength() const {
  const size_t availableSamples = mCircularBuffer.available();
  if (mBaseSamplerate == 0.0f || mPCMformat.channels == 0) return 0.0;
  return static_cast<double>(availableSamples) /
         (static_cast<double>(mBaseSamplerate) *
          static_cast<double>(mPCMformat.channels));
}

uint64_t PullBufferStream::timeToEncodedByteOffset(double seconds) {
  if (seconds <= 0.0) return 0;
  if (mPCMformat.dataType == BufferType::AUTO ||
      mPCMformat.dataType == BufferType::OPUS) {
    if (mStreamDecoder) {
      const uint64_t decoderOffset = mStreamDecoder->timeToByteOffset(seconds);
      if (decoderOffset != 0 && mStreamDecoder->canSeekToTime(seconds)) {
        return decoderOffset;
      }
      // The decoder's seek table doesn't cover the target yet. If the total
      // duration is known, estimate the encoded byte offset proportionally.
      // This lets out-of-buffer seeks resolve quickly for formats like Ogg
      // without waiting for the full seek table to be built.
      if (mProbedDuration > 0.0 && mAudioSizeBytes > 0) {
        const double ratio = seconds / mProbedDuration;
        const uint64_t estimated = static_cast<uint64_t>(
            ratio * static_cast<double>(mAudioSizeBytes));
        return std::min(estimated, mAudioSizeBytes - 1);
      }
    }
    return 0;
  }
  const double bytesPerSecond = static_cast<double>(mPCMformat.sampleRate) *
                                static_cast<double>(mPCMformat.channels) *
                                static_cast<double>(mPCMformat.bytesPerSample);
  return static_cast<uint64_t>(seconds * bytesPerSecond);
}

AudioMetadataFFI
PullBufferStream::convertMetadataToFFI(const AudioMetadata &metadata) const {
  AudioMetadataFFI ffi = {};
  switch (metadata.type) {
  case BUFFER_OGG_OPUS:
    ffi.detectedType = DetectedTypeFFI::OGG_OPUS;
    break;
  case BUFFER_OGG_VORBIS:
    ffi.detectedType = DetectedTypeFFI::OGG_VORBIS;
    break;
  case BUFFER_OGG_FLAC:
  case BUFFER_FLAC:
    ffi.detectedType = DetectedTypeFFI::OGG_FLAC;
    break;
  case BUFFER_MP3_WITH_ID3:
    ffi.detectedType = DetectedTypeFFI::MP3_WITH_ID3;
    break;
  case BUFFER_MP3_STREAM:
    ffi.detectedType = DetectedTypeFFI::MP3_STREAM;
    break;
  case BUFFER_WAV:
    ffi.detectedType = DetectedTypeFFI::WAV;
    break;
  default:
    ffi.detectedType = DetectedTypeFFI::UNKNOWN;
  }

  std::strncpy(ffi.mp3Metadata.title, metadata.mp3Metadata.title.c_str(),
               MAX_STRING_LENGTH - 1);
  std::strncpy(ffi.mp3Metadata.artist, metadata.mp3Metadata.artist.c_str(),
               MAX_STRING_LENGTH - 1);
  std::strncpy(ffi.mp3Metadata.album, metadata.mp3Metadata.album.c_str(),
               MAX_STRING_LENGTH - 1);
  std::strncpy(ffi.mp3Metadata.date, metadata.mp3Metadata.date.c_str(),
               MAX_STRING_LENGTH - 1);
  std::strncpy(ffi.mp3Metadata.genre, metadata.mp3Metadata.genre.c_str(),
               MAX_STRING_LENGTH - 1);

  std::strncpy(ffi.oggMetadata.vendor, metadata.oggMetadata.vendor.c_str(),
               MAX_STRING_LENGTH - 1);
  ffi.oggMetadata.commentsCount =
      static_cast<int>(std::min(metadata.oggMetadata.comments.size(),
                                static_cast<size_t>(MAX_COMMENTS)));

  int i = 0;
  for (const auto &comment : metadata.oggMetadata.comments) {
    if (i >= MAX_COMMENTS) break;
    std::strncpy(ffi.oggMetadata.comments[i].key, comment.first.c_str(),
                 MAX_STRING_LENGTH - 1);
    std::strncpy(ffi.oggMetadata.comments[i].value, comment.second.c_str(),
                 MAX_STRING_LENGTH - 1);
    ++i;
  }

  ffi.oggMetadata.vorbisInfo = {
      metadata.oggMetadata.vorbisInfo.version,
      metadata.oggMetadata.vorbisInfo.channels,
      metadata.oggMetadata.vorbisInfo.rate,
      metadata.oggMetadata.vorbisInfo.bitrate_upper,
      metadata.oggMetadata.vorbisInfo.bitrate_nominal,
      metadata.oggMetadata.vorbisInfo.bitrate_lower,
      metadata.oggMetadata.vorbisInfo.bitrate_window};
  ffi.oggMetadata.opusInfo = {
      metadata.oggMetadata.opusInfo.version,
      metadata.oggMetadata.opusInfo.channels,
      metadata.oggMetadata.opusInfo.pre_skip,
      metadata.oggMetadata.opusInfo.input_sample_rate,
      metadata.oggMetadata.opusInfo.output_gain,
      metadata.oggMetadata.opusInfo.mapping_family,
      metadata.oggMetadata.opusInfo.stream_count,
      metadata.oggMetadata.opusInfo.coupled_count,
      {0},
      static_cast<int>(metadata.oggMetadata.opusInfo.channel_mapping.size())};
  for (size_t j = 0;
       j < metadata.oggMetadata.opusInfo.channel_mapping.size() &&
       j < MAX_CHANNEL_MAPPING;
       ++j) {
    ffi.oggMetadata.opusInfo.channel_mapping[j] =
        metadata.oggMetadata.opusInfo.channel_mapping[j];
  }
  ffi.oggMetadata.flacInfo = {
      metadata.oggMetadata.flacInfo.min_blocksize,
      metadata.oggMetadata.flacInfo.max_blocksize,
      metadata.oggMetadata.flacInfo.min_framesize,
      metadata.oggMetadata.flacInfo.max_framesize,
      metadata.oggMetadata.flacInfo.sample_rate,
      metadata.oggMetadata.flacInfo.channels,
      metadata.oggMetadata.flacInfo.bits_per_sample,
      metadata.oggMetadata.flacInfo.total_samples};

  return ffi;
}

} // namespace SoLoud

#include <mutex>
#include <string.h>

#include "../soloud_common.h"
#include "audiobuffer.h"
#include "metadata_ffi.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#ifdef MA_ENABLE_AUDIO_WORKLETS
#include <emscripten/threading.h>
#include <emscripten/webaudio.h>
#endif
#endif

// TODO: readSamplesFromBuffer as for waveform

namespace SoLoud {
std::mutex check_buffer_mutex;

#ifdef MA_ENABLE_AUDIO_WORKLETS
// Main-thread entry points for the stream event callbacks. On the
// multi-threaded (AudioWorklet) build the callbacks can fire from the
// AudioWorklet rendering thread, which has no `window` and where
// MAIN_THREAD_ASYNC_EM_ASM would execute in the worklet's own JS realm
// (ReferenceError: window is not defined). The callers route through
// emscripten_audio_worklet_post_function_* so these run on the main
// browser thread.

// Static slot for the metadata struct: heap-allocating it on the audio
// thread could take the contended heap lock, and a futex wait there aborts
// the program. Metadata callbacks are rare (once per stream open), so a
// last-wins static slot is fine.
static AudioMetadataFFI gBsMetadataSlot;

static void bsOnMetadataMainThread(int metadataPtr, int soundHash)
{
  EM_ASM(
      {
        var functionName = "dartOnMetadataCallback_" + $1;
        if (typeof window[functionName] === "function") {
          window[functionName]($0); // Call it with the pointer
        } else {
        }
      },
      metadataPtr, soundHash);
}

static void bsOnBufferingMainThread(int isBuffering, int handle, double time,
                                    int soundHash)
{
  EM_ASM(
      {
        var functionName = "dartOnBufferingCallback_" + $3;
        if (typeof window[functionName] === "function") {
          var buffering = $0 == 1 ? true : false;
          window[functionName](buffering, $1, $2); // Call it
        } else {
          console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
        }
      },
      isBuffering, handle, time, soundHash);
}
#endif // MA_ENABLE_AUDIO_WORKLETS

static void clearPlanarBuffer(float *buffer, unsigned int frames,
                              unsigned int stride, unsigned int channels) {
  if (buffer == nullptr || frames == 0 || channels == 0) {
    return;
  }
  for (unsigned int channel = 0; channel < channels; ++channel) {
    memset(buffer + channel * stride, 0, sizeof(float) * frames);
  }
}

BufferStreamInstance::BufferStreamInstance(BufferStream *aParent) {
  mParent = aParent;
  mOffset = 0;
  samplerateAlreadySet = false;
  if (mParent != nullptr && mParent->autoTypeSamplerate != 0.f) {
    mBaseSamplerate = mParent->autoTypeSamplerate;
    mSamplerate = mParent->autoTypeSamplerate;
    mChannels = mParent->autoTypeChannels;
    samplerateAlreadySet = true;
  }
}

BufferStreamInstance::~BufferStreamInstance() {}

// ###### flutter_soloud local patch (retroactive re-mix) ######
class BufferStreamSourceStateSnapshot : public SoLoud::SourceStateSnapshot {
public:
  unsigned int mOffset;
  bool samplerateAlreadySet;
};

SoLoud::SourceStateSnapshot *BufferStreamInstance::captureSourceState() {
  // Only PRESERVED streams keep the consumed data around to re-read.
  if (mParent == nullptr ||
      mParent->mBuffer.bufferingType != BufferingType::PRESERVED) {
    return nullptr;
  }
  auto *s = new BufferStreamSourceStateSnapshot();
  s->mOffset = mOffset;
  s->samplerateAlreadySet = samplerateAlreadySet;
  return s;
}

void BufferStreamInstance::restoreSourceState(
    SoLoud::SourceStateSnapshot *aState) {
  auto *s = static_cast<BufferStreamSourceStateSnapshot *>(aState);
  if (s == nullptr) {
    return;
  }
  mOffset = s->mOffset;
  samplerateAlreadySet = s->samplerateAlreadySet;
}


unsigned int BufferStreamInstance::getAudio(float *aBuffer,
                                            unsigned int aSamplesToRead,
                                            unsigned int aBufferSize) {
  if (aBuffer == nullptr || mChannels == 0 || aSamplesToRead == 0) {
    return 0;
  }

  // Check if parent is still valid before accessing it
  if (mParent == nullptr || !mParent->isValid()) {
    clearPlanarBuffer(aBuffer, aSamplesToRead, aBufferSize, mChannels);
    return 0;
  }

  std::lock_guard<std::recursive_mutex> lock(mParent->mBuffer.bufferMutex);

  // When using BufferType::AUTO, samplerate and channels are got from the
  // stream. Hence we need to update them regardless of how are set by
  // setBufferStream. But these parameters need to be set after the play
  // function is called and the instance of this class is created.
  if (!samplerateAlreadySet && mParent->autoTypeSamplerate != 0.f) {
    mBaseSamplerate = mParent->autoTypeSamplerate;
    mSamplerate = mParent->autoTypeSamplerate;
    mChannels = mParent->autoTypeChannels;
    samplerateAlreadySet = true;
  }

  const unsigned int bufferSize =
      static_cast<unsigned int>(mParent->mBuffer.getFloatsBufferSize());
  int samplesToRead = 0;
  if (mOffset < bufferSize) {
    const unsigned int endOffset = mOffset + aSamplesToRead * mChannels;
    if (endOffset <= bufferSize) {
      samplesToRead = static_cast<int>(aSamplesToRead);
    } else {
      samplesToRead = static_cast<int>((bufferSize - mOffset) / mChannels);
    }
  }

  // No decoded samples available to play: zero the output and update the
  // stream position. The buffering state will be checked when addData() or
  // setDataIsEnded() is called.
  if (samplesToRead <= 0) {
    clearPlanarBuffer(aBuffer, aSamplesToRead, aBufferSize, mChannels);
    if (mParent->mBuffer.bufferingType == BufferingType::PRESERVED) {
      mStreamPosition = (mBaseSamplerate > 0.0f)
                            ? mOffset / (mBaseSamplerate * mChannels)
                            : 0.0;
    } else {
      mStreamPosition = 0;
    }
    return 0;
  }

  // Zero any frames we won't fill, then copy the active frames.
  if (samplesToRead < static_cast<int>(aSamplesToRead)) {
    clearPlanarBuffer(aBuffer, aSamplesToRead, aBufferSize, mChannels);
  }

  float *buffer = reinterpret_cast<float *>(mParent->mBuffer.buffer.data() +
                                            mParent->mBuffer.getReadOffset());
  if (buffer == nullptr) {
    clearPlanarBuffer(aBuffer, aSamplesToRead, aBufferSize, mChannels);
    return 0;
  }

  if (mChannels == 1) {
    // Optimization: if we have a mono audio source, we can just copy all the
    // data in one go.
    memcpy(aBuffer, buffer + mOffset, sizeof(float) * samplesToRead);
  } else {
    // From SoLoud documentation:
    // So, if 1024 samples are requested from a stereo audio source, the first
    // 1024 floats should be for the first channel, and the next 1024 samples
    // should be for the second channel.
    for (unsigned int ch = 0; ch < mChannels; ++ch) {
      for (int i = 0; i < samplesToRead; ++i) {
        aBuffer[ch * aBufferSize + i] = buffer[mOffset + i * mChannels + ch];
      }
    }
  }

  const unsigned int totalBytesRead =
      samplesToRead * mChannels * sizeof(float);
  const size_t samplesRemoved = mParent->mBuffer.removeData(totalBytesRead);

  // If buffering type is RELEASED, adjust mSampleCount and don't increment
  // mOffset.
  if (mParent->mBuffer.bufferingType == BufferingType::RELEASED) {
    mParent->mSampleCount -= samplesRemoved;
    mStreamPosition = 0;
    mParent->mBytesConsumed += totalBytesRead;
  } else {
    mOffset += samplesToRead * mChannels;
    // For PRESERVED type, streamPosition advances with the offset.
    mStreamPosition = (mBaseSamplerate > 0.0f)
                          ? mOffset / (mBaseSamplerate * mChannels)
                          : 0.0;
  }

  return samplesToRead;
}

result BufferStreamInstance::seek(double aSeconds, float *mScratch,
                                  unsigned int mScratchSize) {
  (void)mScratch;
  (void)mScratchSize;
  if (aSeconds <= 0.0) {
    rewind();
    return SO_NO_ERROR;
  }
  // Check parent validity before accessing
  if (mParent == nullptr || !mParent->isValid()) {
    return INVALID_PARAMETER;
  }
  if (mParent->mBuffer.bufferingType == BufferingType::RELEASED) {
    // Seeking not supported in RELEASED mode since data is discarded
    // TODO: Support seeking forward in RELEASED mode
    return INVALID_PARAMETER;
  }

  // For PRESERVED mode the decoded buffer is kept from the start, so we can
  // jump directly to the target sample. Align the offset down to a whole
  // frame: getAudio() consumes data in units of `mChannels` floats, so an
  // unaligned offset would leave an unplayable sub-frame remainder at the
  // end of the buffer and the voice could never reach its end.
  const int pos = static_cast<int>(floor(mBaseSamplerate * mChannels * aSeconds));
  mOffset = mChannels > 0 ? pos - (pos % static_cast<int>(mChannels)) : pos;
  mStreamPosition = static_cast<float>(mOffset) / (mBaseSamplerate * mChannels);
  return SO_NO_ERROR;
}

result BufferStreamInstance::rewind() {
  mOffset = 0;
  mStreamPosition = 0.0f;
  return 0;
}

bool BufferStreamInstance::hasEnded() {
  // Check parent validity before accessing
  if (mParent == nullptr || !mParent->isValid()) {
    return true;  // Parent destroyed or invalid, consider ended
  }
  if (!mParent->dataIsEnded) {
    return false;
  }
  if (mParent->mBuffer.bufferingType == BufferingType::PRESERVED) {
    // `mOffset` advances in whole frames (`mChannels` floats at a time) and
    // getAudio() cannot play a sub-frame remainder, so being within one
    // frame of the end counts as ended. This covers two cases:
    // - `mSampleCount` overshooting the actual decoded data (for compressed
    //   formats it comes from the decoder's frame-count estimate, e.g.
    //   drmp3_get_pcm_frame_count, which may exceed the frames the decoder
    //   really yields: encoder delay/padding, truncated final frame);
    // - a buffer whose float count is not a multiple of `mChannels`.
    // Without this, a voice reaching such a buffer's end underruns forever
    // and is never stopped.
    const unsigned int slack = mChannels > 0 ? mChannels - 1 : 0;
    return mOffset + slack >= mParent->mSampleCount ||
           mOffset + slack >= mParent->mBuffer.getFloatsBufferSize();
  }
  // RELEASED
  return mParent->mBuffer.getFloatsBufferSize() == 0;
}

// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////
// //////////////////////////////////////////////////////////////

BufferStream::BufferStream() : mIsDestroyed(false) {}

BufferStream::~BufferStream() {
  // stop();
  // resetBuffer();
}

PlayerErrors BufferStream::setBufferStream(
    Player *aPlayer, ActiveSound *aParent, unsigned int maxBufferSize,
    BufferingType bufferingType, SoLoud::time bufferingTimeNeeds,
    PCMformat pcmFormat, dartOnBufferingCallback_t onBufferingCallback,
    dartOnMetadataCallback_t onMetadataCallback) {
  /// maxBufferSize must be a number divisible by channels * sizeof(float)
  if (maxBufferSize % (pcmFormat.channels * sizeof(float)) != 0)
    maxBufferSize -= maxBufferSize % (pcmFormat.channels * sizeof(float));

  // Force OPUS to AUTO since Mp3, Ogg Opus and Ogg Vorbis are auto detected
  // There is no more need to use BufferType::OPUS
  if (pcmFormat.dataType == BufferType::OPUS)
    pcmFormat.dataType = BufferType::AUTO;

  autoTypeChannels = 0;
  autoTypeSamplerate = 0.f;
  mBytesReceived = 0;
  mUncompressedBytesReceived = 0;
  mSampleCount = 0;
  mBytesConsumed = 0;
  dataIsEnded = false;
  mThePlayer = aPlayer;
  mParent = aParent;
  mPCMformat.sampleRate = pcmFormat.sampleRate;
  mPCMformat.channels = pcmFormat.channels;
  mPCMformat.bytesPerSample = pcmFormat.bytesPerSample;
  mPCMformat.dataType = pcmFormat.dataType;
  mBuffer.clear();
  mBuffer.setSizeInBytes(maxBufferSize);
  mMaxBufferSize = maxBufferSize;
  mBufferingTimeNeeds = bufferingTimeNeeds;
  mChannels = pcmFormat.channels;
  mBaseSamplerate = (float)pcmFormat.sampleRate;
  mOnBufferingCallback.store(onBufferingCallback);
  mOnMetadataCallback.store(onMetadataCallback);
  // Recorded with the callables: the gate retires by generation, so a
  // registration made after a retirement must carry the new one.
  mDartCallbackGeneration.store(dart_callbacks::currentGeneration(),
                                std::memory_order_release);
  buffer = std::vector<unsigned char>();
  mBuffer.setBufferType(bufferingType);
  mIsBuffering = true;
  mIsDestroyed = false;
  mIcyMetaInt =
      16000; // for mp3 streaming audio only. Most online streaming use 16000

  if (pcmFormat.dataType == BufferType::AUTO) {
    streamDecoder = std::make_unique<StreamDecoder>();
  }

#if defined(NO_XIPH_LIBS)
  if (pcmFormat.dataType == BufferType::OPUS) {
    return PlayerErrors::failedToCreateOpusDecoder;
  }
#endif

  return PlayerErrors::noError;
}

void BufferStream::resetBuffer() {
  buffer.clear();
  mBuffer.clear();
  mSampleCount = 0;
  mBytesReceived = 0;
  mUncompressedBytesReceived = 0;
  mBytesConsumed = 0;
  dataIsEnded = false;
  mIsBuffering = true;
  autoTypeChannels = 0;
  autoTypeSamplerate = 0.f;
  if (streamDecoder) {
    streamDecoder = std::make_unique<StreamDecoder>();
    streamDecoder->setBufferIcyMetaInt(mIcyMetaInt);
  }

  for (int i = 0; i < mParent->handle.size(); i++) {
    mThePlayer->soloud.seek(mParent->handle[i].handle, 0.0f);
    mParent->handle[i].bufferingTime = 0.0f;
  }
}

void BufferStream::setDataIsEnded() {
  // Signal the decoder that no more data will be added.
  // This must happen BEFORE the final addData call so the decoder
  // knows to flush all remaining audio from its internal buffers.
  if (streamDecoder) {
    streamDecoder->setDataEnded();
  }

  // Trigger a final decode pass to flush any remaining data.
  // This is needed even if buffer.size() is 0, because the underlying
  // decoder (e.g., dr_mp3) may have data in its internal buffer that
  // hasn't been decoded yet.
  addData(nullptr, 0, true);

  buffer.clear();
  dataIsEnded = true;
  checkBuffering(0);
}

void BufferStream::setBufferIcyMetaInt(int icyMetaInt) {
  mIcyMetaInt = icyMetaInt;
  if (streamDecoder) {
    streamDecoder->setBufferIcyMetaInt(icyMetaInt);
  }
}

PlayerErrors BufferStream::addData(const void *aData, unsigned int aDataLen,
                                   bool dontAdd) {
  if (dataIsEnded) {
    return PlayerErrors::streamEndedAlready;
  }

  size_t bytesWritten = 0;
  bool allDataAdded = false;
  int32_t bufferDataToAdd = 0;

  if (!dontAdd) {
    buffer.insert(buffer.end(), static_cast<const unsigned char *>(aData),
                  static_cast<const unsigned char *>(aData) + aDataLen);
    mBytesReceived += aDataLen;
    // For PCM data we must align the data to the bytes per sample.
    if (mPCMformat.dataType != BufferType::AUTO) {
      int alignment = mPCMformat.bytesPerSample * mPCMformat.channels;
      bufferDataToAdd = (int)(buffer.size() / alignment) * alignment;
    } else {
      // Performing some buffering. We need some data to be added expecially
      // when using opus or mp3.
      if (buffer.size() > 1024 * 4) // 4 KB of data.
      {
        // When using opus,ogg or mp3 we don't need to align.
        bufferDataToAdd = static_cast<int32_t>(buffer.size());
      } else {
        // Return if there is not enough data to add.
        return PlayerErrors::noError;
      }
    }

  } else {
    bufferDataToAdd = static_cast<int32_t>(buffer.size());
  }

  // It's time to decode the data already stored in the buffer
  if (mPCMformat.dataType == BufferType::AUTO) {
    int sampleRate = mThePlayer->mSampleRate;
    int channels = mThePlayer->mChannels;
    // Ogg Opus will decode to the sampleRate and channels of the current engine
    // settings and the AudioSource will be set to use them. For the mp3 this
    // AudioSource will impose the mp3 settings (the engine will convert to its
    // settings).
    auto [decoded, error] = streamDecoder->decode(
        buffer, &sampleRate, &channels, [&](AudioMetadata meta) {
          //   meta.debug();
          if (this->mOnMetadataCallback != nullptr)
            this->callOnMetadataCallback(meta);
        });

    // Handle decoder errors
    switch (error) {
    case DecoderError::FormatNotSupported:
      return PlayerErrors::audioFormatNotSupported;
    case DecoderError::NoXiphLibs:
      return PlayerErrors::xiphLibsNotFound;
    case DecoderError::FailedToCreateDecoder:
      return PlayerErrors::failedToCreateOpusDecoder;
    case DecoderError::ErrorReadingOggOpusPage:
      return PlayerErrors::failedToDecodeOpusPacket;
    default:
      break;
    }

    if (!decoded.empty()) {
      if (autoTypeSamplerate == 0.f) {
        if (sampleRate != -1) {
          mPCMformat.sampleRate = sampleRate;
          mBaseSamplerate = sampleRate;
          autoTypeSamplerate = sampleRate;
        }
        if (channels != -1) {
          mPCMformat.channels = channels;
          mChannels = channels;
          autoTypeChannels = channels;
        }
      }

      bytesWritten = mBuffer.addData(BufferType::PCM_F32LE, decoded.data(),
                                     decoded.size(), &allDataAdded) *
                     sizeof(float);
    } else {
      // Continue buffering. Maybe we are still adding artwork image data.
      return PlayerErrors::noError;
    }
  } else {
    // PCM data
    bytesWritten = mBuffer.addData(mPCMformat.dataType, buffer.data(),
                                   bufferDataToAdd / mPCMformat.bytesPerSample,
                                   &allDataAdded) *
                   sizeof(float);
    // Remove only the source bytes that were actually converted and written.
    if (bytesWritten > 0) {
      const size_t samplesWritten = bytesWritten / sizeof(float);
      const size_t sourceBytesConsumed =
          samplesWritten * mPCMformat.bytesPerSample;
      buffer.erase(buffer.begin(), buffer.begin() + sourceBytesConsumed);
    }
  }

  checkBuffering(static_cast<unsigned int>(bytesWritten));
  mUncompressedBytesReceived += bytesWritten;

  mSampleCount += static_cast<unsigned int>(bytesWritten / sizeof(float));

  // data has been added to the buffer, but not all because reached its full
  // capacity. So mark this stream as ended and no more data can be added.
  if (!allDataAdded) {
    dataIsEnded = true;
    return PlayerErrors::pcmBufferFull;
  }

  return PlayerErrors::noError;
}

/// Check if some handles was paused for buffering and unpause them or restart
/// them if needed after adding [afterAddingBytesCount] bytes.
void BufferStream::checkBuffering(unsigned int afterAddingBytesCount) {
  std::lock_guard<std::mutex> lock(check_buffer_mutex);

  // If a handle reaches the end and data is not ended, we have to wait for it
  // has enough data to reach [TIME_FOR_BUFFERING] and restart playing it.
  SoLoud::time currBufferTime = getLength();
  SoLoud::time addedDataTime =
      (afterAddingBytesCount / sizeof(float)) /
      (mBaseSamplerate * mChannels);

  for (int i = 0; i < mParent->handle.size(); i++) {
    SoLoud::handle handle = mParent->handle[i].handle;
    SoLoud::time pos = mBuffer.bufferingType == BufferingType::RELEASED
                           ? getStreamTimeConsumed()
                           : mThePlayer->getPosition(handle);
    bool isPaused = mThePlayer->getPause(handle);

    // This handle needs to wait for [TIME_FOR_BUFFERING]. Pause it.
    // Pause only when the play position has reached the end of the data that
    // was already buffered before this addData() call. The unpause below will
    // then wait until at least [bufferingTimeNeeds] seconds of audio are
    // available ahead of position.
    if (mBuffer.bufferingType == BufferingType::RELEASED &&
        !dataIsEnded && pos >= currBufferTime && !isPaused) {
      mParent->handle[i].bufferingTime = currBufferTime + addedDataTime;
      // This is an automatic buffering pause, so the user-paused flag should
      // not prevent a future buffering unpause.
      mParent->handle[i].isUserPaused = false;
      mThePlayer->setPause(handle, true, false);
      isPaused = true;
      callOnBufferingCallback(true, handle, currBufferTime + addedDataTime);
    } else
    // This handle has reached [TIME_FOR_BUFFERING]. Unpause it.
    // Only unpause when buffer covers playback position + margin,
    // not just when new data >= margin (which caused play/pause toggling
    // when seeking beyond buffered data).
    // Also respect a user-initiated pause: if the user pressed pause, do not
    // automatically resume even when enough data is buffered.
    if (currBufferTime + addedDataTime >= pos + mBufferingTimeNeeds && isPaused &&
        !mParent->handle[i].isUserPaused){
        mParent->handle[i].bufferingTime = currBufferTime + addedDataTime;
        mThePlayer->setPause(handle, false, false);
        isPaused = false;
        callOnBufferingCallback(false, handle, currBufferTime + addedDataTime);
      } else if (isPaused && mParent->handle[i].isUserPaused) {
        if (currBufferTime + addedDataTime >= pos + mBufferingTimeNeeds && mIsBuffering) {
          mParent->handle[i].bufferingTime = currBufferTime + addedDataTime;
          callOnBufferingCallback(false, handle, currBufferTime + addedDataTime);
        } else {
          // fprintf(stderr, "[checkBuffering] -> STAY PAUSED handle=%u (user paused)\n", handle);
        }
      }
    // If data is ended and the handle is paused, unpause it to listen to the
    // rest of the data. This also clears the user-paused flag so that a
    // user-paused stream drains its remaining buffer when the stream ends.
    if (dataIsEnded && isPaused && !mParent->handle[i].isUserPaused) {
      mThePlayer->setPause(handle, false, false);
      isPaused = false;
      mParent->handle[i].bufferingTime = MAX_DOUBLE;
      callOnBufferingCallback(false, handle, currBufferTime);
    }
  }
}

void BufferStream::callOnMetadataCallback(AudioMetadata &metadata) {
  // Gated, not merely null-checked: a bare load-then-call can invoke a
  // callable whose isolate has gone away between the two. The pass also
  // blocks a concurrent retirement from returning mid-invocation.
  const dart_callbacks::InvocationPass pass;
  if (!pass.isLive(mDartCallbackGeneration.load(std::memory_order_acquire)))
    return;
  auto metadataCb = mOnMetadataCallback.load();
  if (metadataCb != nullptr) {
    AudioMetadataFFI ffi = this->convertMetadataToFFI(metadata);
    // metadata.debug();
#ifdef __EMSCRIPTEN__
    // Call the Dart callback stored on globalThis, if it exists.
    // The `dartOnMetadataCallback_$hash` function is created in
    // `setBufferStream()` in `bindings_player_web.dart` and it's
    // meant to call the Dart callback passed to `setBufferStream()`.
    // It will pass the JS pointer to the AudioMetadata struct.
#ifdef MA_ENABLE_AUDIO_WORKLETS
    if (!emscripten_is_main_browser_thread()) {
      // No heap allocation on the audio thread (see gBsMetadataSlot).
      gBsMetadataSlot = ffi;
      emscripten_audio_worklet_post_function_sig(
          EMSCRIPTEN_AUDIO_MAIN_THREAD, (void *)bsOnMetadataMainThread, "ii",
          (int)(uintptr_t)&gBsMetadataSlot, (int)mParent->soundHash);
      return;
    }
    bsOnMetadataMainThread((int)(uintptr_t)&ffi, (int)mParent->soundHash);
#else
    // Single-threaded build: everything runs on the main browser thread.
    auto *ffiPtr = static_cast<AudioMetadataFFI *>(malloc(sizeof(ffi)));
    if (ffiPtr == nullptr) return;
    *ffiPtr = ffi;
    MAIN_THREAD_ASYNC_EM_ASM(
        {
          // Compose the function name for this soundHash
          var functionName = "dartOnMetadataCallback_" + $1;
          if (typeof window[functionName] === "function") {
            window[functionName]($0); // Call it with the pointer
          } else {
          }
          Module_soloud._free($0);
        },
        ffiPtr, mParent->soundHash);
#endif
#else
    metadataCb(ffi);
#endif
  }
}

void BufferStream::callOnBufferingCallback(bool isBuffering,
                                           unsigned int handle, double time) {
  // Scoped around the invocation only: `mIsBuffering` below is native state
  // and must keep tracking the stream whether or not a Dart isolate is
  // listening.
  {
    // Gated, not merely null-checked: a bare load-then-call can invoke a
    // callable whose isolate has gone away between the two. The pass also
    // blocks a concurrent retirement from returning mid-invocation.
    const dart_callbacks::InvocationPass pass;
    auto bufferingCb =
        pass.isLive(mDartCallbackGeneration.load(std::memory_order_acquire))
            ? mOnBufferingCallback.load()
            : nullptr;
    if (bufferingCb != nullptr) {
#ifdef __EMSCRIPTEN__
    // Call the Dart callback stored on globalThis, if it exists.
    // The `dartOnBufferingCallback_$hash` function is created in
    // `setBufferStream()` in `bindings_player_web.dart` and it's
    // meant to call the Dart callback passed to `setBufferStream()`.
    // This event is used for this.
#ifdef MA_ENABLE_AUDIO_WORKLETS
    if (!emscripten_is_main_browser_thread()) {
      // See bsOnBufferingMainThread: on the AudioWorklet rendering thread
      // `window` does not exist. All arguments are passed by value.
      emscripten_audio_worklet_post_function_sig(
          EMSCRIPTEN_AUDIO_MAIN_THREAD, (void *)bsOnBufferingMainThread,
          "iidi", (int)isBuffering, (int)handle, time,
          (int)mParent->soundHash);
      return;
    }
    bsOnBufferingMainThread((int)isBuffering, (int)handle, time,
                            (int)mParent->soundHash);
#else
    MAIN_THREAD_ASYNC_EM_ASM(
        {
          // Compose the function name for this soundHash
          var functionName = "dartOnBufferingCallback_" + $3;
          if (typeof window[functionName] === "function") {
            var buffering = $0 == 1 ? true : false;
            window[functionName](buffering, $1, $2); // Call it
          } else {
            console.log("EM_ASM 'dartOnBufferingCallback_$hash' not found.");
          }
        },
        isBuffering, handle, time, mParent->soundHash);
#endif
#else
      bufferingCb(isBuffering, handle, time);
#endif
    }
  }
  mIsBuffering = isBuffering;
}

void BufferStream::clearDartCallbacks() {
  mDartCallbackGeneration.store(dart_callbacks::kNoGeneration,
                                std::memory_order_release);
  mOnBufferingCallback.store(nullptr);
  mOnMetadataCallback.store(nullptr);
}

BufferingType BufferStream::getBufferingType() { return mBuffer.bufferingType; }

SoLoud::time BufferStream::getLength() {
  if (mBaseSamplerate == 0 || mUncompressedBytesReceived == 0)
    return 0;
  // the internal buffer always stores float samples
  return (mUncompressedBytesReceived / sizeof(float)) /
         (mBaseSamplerate * mPCMformat.channels);
}

/// Get the time consumed by this stream of type RELEASED
SoLoud::time BufferStream::getStreamTimeConsumed() {
  // the internal buffer always stores float samples
  return (double)(mBytesConsumed / sizeof(float)) /
         (mBaseSamplerate * mPCMformat.channels);
}

AudioMetadataFFI
BufferStream::convertMetadataToFFI(const AudioMetadata &metadata) {
  AudioMetadataFFI ffi = {};

  // Set detected type
  switch (metadata.type) {
  case BUFFER_OGG_OPUS:
    ffi.detectedType = DetectedTypeFFI::OGG_OPUS;
    break;
  case BUFFER_OGG_VORBIS:
    ffi.detectedType = DetectedTypeFFI::OGG_VORBIS;
    break;
  case BUFFER_OGG_FLAC:
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

  // Convert MP3 metadata
  strncpy(ffi.mp3Metadata.title, metadata.mp3Metadata.title.c_str(),
          MAX_STRING_LENGTH - 1);
  strncpy(ffi.mp3Metadata.artist, metadata.mp3Metadata.artist.c_str(),
          MAX_STRING_LENGTH - 1);
  strncpy(ffi.mp3Metadata.album, metadata.mp3Metadata.album.c_str(),
          MAX_STRING_LENGTH - 1);
  strncpy(ffi.mp3Metadata.date, metadata.mp3Metadata.date.c_str(),
          MAX_STRING_LENGTH - 1);
  strncpy(ffi.mp3Metadata.genre, metadata.mp3Metadata.genre.c_str(),
          MAX_STRING_LENGTH - 1);

  // Convert OGG metadata
  strncpy(ffi.oggMetadata.vendor, metadata.oggMetadata.vendor.c_str(),
          MAX_STRING_LENGTH - 1);
  ffi.oggMetadata.commentsCount =
      MIN((int)metadata.oggMetadata.comments.size(), MAX_COMMENTS);

  int i = 0;
  for (const auto &comment : metadata.oggMetadata.comments) {
    if (i >= MAX_COMMENTS)
      break;
    strncpy(ffi.oggMetadata.comments[i].key, comment.first.c_str(),
            MAX_STRING_LENGTH - 1);
    strncpy(ffi.oggMetadata.comments[i].value, comment.second.c_str(),
            MAX_STRING_LENGTH - 1);
    i++;
  }

  // Convert Vorbis info
  ffi.oggMetadata.vorbisInfo = {metadata.oggMetadata.vorbisInfo.version,
                                metadata.oggMetadata.vorbisInfo.channels,
                                metadata.oggMetadata.vorbisInfo.rate,
                                metadata.oggMetadata.vorbisInfo.bitrate_upper,
                                metadata.oggMetadata.vorbisInfo.bitrate_nominal,
                                metadata.oggMetadata.vorbisInfo.bitrate_lower,
                                metadata.oggMetadata.vorbisInfo.bitrate_window};

  // Convert Opus info
  ffi.oggMetadata.opusInfo = {
      metadata.oggMetadata.opusInfo.version,
      metadata.oggMetadata.opusInfo.channels,
      metadata.oggMetadata.opusInfo.pre_skip,
      metadata.oggMetadata.opusInfo.input_sample_rate,
      metadata.oggMetadata.opusInfo.output_gain,
      metadata.oggMetadata.opusInfo.mapping_family,
      metadata.oggMetadata.opusInfo.stream_count,
      metadata.oggMetadata.opusInfo.coupled_count,
      {0}, // Initialize channel_mapping array to zeros
      (int)metadata.oggMetadata.opusInfo.channel_mapping.size()};

  // Convert Flac info
  ffi.oggMetadata.flacInfo = {metadata.oggMetadata.flacInfo.min_blocksize,
                              metadata.oggMetadata.flacInfo.max_blocksize,
                              metadata.oggMetadata.flacInfo.min_framesize,
                              metadata.oggMetadata.flacInfo.max_framesize,
                              metadata.oggMetadata.flacInfo.sample_rate,
                              metadata.oggMetadata.flacInfo.channels,
                              metadata.oggMetadata.flacInfo.bits_per_sample,
                              metadata.oggMetadata.flacInfo.total_samples};

  // Also populate FLAC info for raw FLAC streams
  if (metadata.type == DetectedType::BUFFER_FLAC) {
    ffi.detectedType = DetectedTypeFFI::OGG_FLAC;
  }

  // Copy channel mapping
  for (size_t i = 0; i < metadata.oggMetadata.opusInfo.channel_mapping.size() &&
                     i < MAX_CHANNEL_MAPPING;
       i++) {
    ffi.oggMetadata.opusInfo.channel_mapping[i] =
        metadata.oggMetadata.opusInfo.channel_mapping[i];
  }

  return ffi;
}

AudioSourceInstance *BufferStream::createInstance() {
  return new BufferStreamInstance(this);
}
}; // namespace SoLoud

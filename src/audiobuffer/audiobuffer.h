#pragma once

#ifndef _AUDIOBUFFER_H
#define _AUDIOBUFFER_H

#include "../active_sound.h"
#include "../enums.h"
#include "../player.h"
#include "../soloud/include/soloud.h"
#include "../dart_callback_gate.h"
#include "buffer.h"
#include <atomic>
#include <chrono>
#include <thread>
#include <stdio.h>
#if !defined(NO_XIPH_LIBS)
#include "opus_stream_decoder.h"
#endif
#include "metadata_ffi.h"
#include "mp3_stream_decoder.h"
#include "stream_decoder.h"
#include "wav_stream_decoder.h"

class Player;

namespace SoLoud {
class BufferStream;

class BufferStreamInstance : public AudioSourceInstance {
  BufferStream *mParent;
  unsigned int mOffset;

public:
  BufferStreamInstance(BufferStream *aParent);
  virtual unsigned int getAudio(float *aBuffer, unsigned int aSamplesToRead,
                                unsigned int aBufferSize);
  virtual result seek(double aSeconds, float *mScratch,
                      unsigned int mScratchSize);
  virtual result rewind();
  virtual bool hasEnded();
  virtual ~BufferStreamInstance();
  // ###### flutter_soloud local patch (retroactive re-mix) ######
  // PRESERVED streams keep the whole decoded buffer, so the consumption state
  // is exactly mOffset and the voice can be re-mixed. RELEASED streams discard
  // consumed data and return nullptr (going-forward only, plan §7).
  virtual SoLoud::SourceStateSnapshot *captureSourceState();
  virtual void restoreSourceState(SoLoud::SourceStateSnapshot *aState);
  virtual bool captureSourceStateInto(SoLoud::SourceStateSnapshot *aState);
  bool samplerateAlreadySet;
  /// Set by the mixer when it pauses this voice for running out of data, and
  /// taken by the next buffering check (BufferStream::takeBufferingPause()).
  std::atomic<bool> mPausedForBuffering{false};
  /// The parent's mFormatGeneration when this voice adopted its format.
  uint64_t mFormatGeneration{0};

private:
  /// This voice's handle, from SoLoud's voice table: 0 if it is not there.
  /// SoLoud's audio mutex is held (the mixer's, in getAudio()).
  SoLoud::handle findOwnHandleLocked() const;
};

class BufferStream : public AudioSource {
public:
  // The flutter_soloud main [player] instance
  Player *mThePlayer;
  // Used to access the AudioSource this stream belongs to
  ActiveSound *mParent;
  std::atomic<dartOnBufferingCallback_t> mOnBufferingCallback{nullptr};
  std::atomic<dartOnMetadataCallback_t> mOnMetadataCallback{nullptr};
  /// The Dart callback generation the two callables above were registered at.
  /// Checked on every invocation so a retirement makes them inert without
  /// having to reach into this object. See `src/dart_callback_gate.h`.
  std::atomic<uint64_t> mDartCallbackGeneration{dart_callbacks::kNoGeneration};
  unsigned int autoTypeChannels;
  float autoTypeSamplerate;
  /// Advanced by a reset, under the audio mutex and the buffer mutex. A voice
  /// compares the generation it adopted its format at under the buffer mutex
  /// before it relies on that format.
  uint64_t mFormatGeneration{0};
  unsigned int mMaxBufferSize;
  // The counters and the end flag are updated by a delivery (under
  // sounds_mutex), by the background decode thread and, for a RELEASED
  // stream, by the mixer, and read by all of them: atomic.
  std::atomic<int64_t> mSampleCount{0};
  std::atomic<uint64_t> mBytesConsumed{0};
  SoLoud::time mBufferingTimeNeeds;
  PCMformat mPCMformat;
  Buffer mBuffer;
  uint64_t mBytesReceived;
  std::atomic<uint64_t> mUncompressedBytesReceived{0};
  std::atomic<bool> dataIsEnded{false};
  std::atomic<bool> mIsBuffering{true};
  int mIcyMetaInt;
  
  // Flag to indicate the BufferStream is being destroyed
  std::atomic<bool> mIsDestroyed{false};

#ifndef __EMSCRIPTEN__
  std::thread mBackgroundDecodeThread;
  std::atomic<bool> mStopBackgroundDecode{false};
#endif
  void stopBackgroundDecode();

  std::unique_ptr<StreamDecoder> streamDecoder;

  BufferStream();
  virtual ~BufferStream();
  PlayerErrors
  setBufferStream(Player *aPlayer, ActiveSound *aParent,
                  unsigned int maxBufferSize = 1024 * 1024 * 100, // 100 Mbytes
                  BufferingType bufferingType = BufferingType::PRESERVED,
                  time bufferingTimeNeeds = 2.0f, // 2 seconds of data to wait
                  PCMformat pcmFormat = {44100, 2, 2, PCM_S16LE},
                  dartOnBufferingCallback_t onBufferingCallback = nullptr,
                  dartOnMetadataCallback_t onMetadataCallback = nullptr);
  void resetBuffer();
  void setDataIsEnded();
  void setBufferIcyMetaInt(int icyMetaInt);
  PlayerErrors addData(const void *aData, unsigned int numSamples,
                       bool forceAdd = false);
  /// Pause the voices that ran out of data and unpause the ones that have
  /// enough again. It reads and updates the sound's handle list, which a
  /// play() appends to and a voice's end removes from under
  /// Player::sounds_mutex, so it takes that mutex (before
  /// check_buffer_mutex, as a delivery holds them).
  void checkBuffering(unsigned int afterAddingBytesCount);
  /// checkBuffering() for the background decode thread, which a reset or a
  /// disposal stops and joins while holding sounds_mutex: it only tries the
  /// mutex, and gives up once the thread is being stopped. Returns false,
  /// having checked nothing, when it is.
  bool checkBufferingFromDecodeThread(unsigned int afterAddingBytesCount);
  /// Whether the mixer paused voice [aHandle] for running out of data since
  /// this was last asked, clearing the mark. Each buffering check asks: the
  /// voice was then playing, so a user pause no longer holds it, but the
  /// mixer leaves the sound's handle list, and its user-paused flag, alone.
  /// A pause or an unpause by the user asks too, and discards the mark, since
  /// it decides over a buffering pause made before it. Takes SoLoud's audio
  /// mutex.
  bool takeBufferingPause(SoLoud::handle aHandle);
  void callOnMetadataCallback(AudioMetadata &metadata);
  void callOnBufferingCallback(bool isBuffering, unsigned int handle,
                               double time);
  void clearDartCallbacks();
  BufferingType getBufferingType();
  virtual AudioSourceInstance *createInstance();
  
  // Check if the BufferStream is still valid (not being destroyed)
  bool isValid() const { return !mIsDestroyed.load(); }
  
  // Mark the stream as being destroyed - call before destruction
  void markForDestruction() { mIsDestroyed.store(true); }
  SoLoud::time getLength();
  SoLoud::time getStreamTimeConsumed();
  AudioMetadataFFI convertMetadataToFFI(const AudioMetadata &metadata);

  std::vector<unsigned char> buffer;

private:
  /// The body of checkBuffering(), run with sounds_mutex held.
  void checkBufferingHoldingSoundsLock(unsigned int afterAddingBytesCount);
};
}; // namespace SoLoud

#endif // _AUDIOBUFFER_H
